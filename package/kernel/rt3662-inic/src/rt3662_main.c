// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sphairon/Zyxel Speedlink 5501 RT3662-iNIC host driver
 *
 * The RT3662/RT5392 WLAN is not exposed as a conventional PCI radio on this
 * board.  The VR9 host talks to the iNIC firmware over the GSWIP/MII data
 * path using the Ralink RACFG protocol (ethertype 0xffff).  This driver keeps
 * that transport but exposes native cfg80211/nl80211 to OpenWrt.
 */
#include <linux/delay.h>
#include <linux/if_arp.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/version.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>
#include <linux/rtnetlink.h>
#include <net/net_namespace.h>
#include <lantiq_soc.h>

#include "rt3662_inic.h"

static char *miimaster = "wifi";
module_param(miimaster, charp, 0444);
MODULE_PARM_DESC(miimaster, "MII master netdev carrying the RT3662 iNIC protocol");

static unsigned int radios = 2;
module_param(radios, uint, 0444);
MODULE_PARM_DESC(radios, "Number of concurrent iNIC radios (SL5501: 2)");

/* Compatibility names and defaults from Sphairon's rtXXXX_iNIC 2.7.0.5
 * invocation.  The SL5501 concurrent AP script explicitly uses
 * bridge=1 syncmiimac=0; csumoff defaults to 0 and max_fw_upload to 5. */
static bool bridge = true;
module_param(bridge, bool, 0644);
MODULE_PARM_DESC(bridge, "OEM __BUILT_IN_BRIDGE profile attribute");

static bool syncmiimac;
module_param(syncmiimac, bool, 0644);
MODULE_PARM_DESC(syncmiimac, "OEM MII MAC synchronization (SL5501 concurrent default: 0)");

static bool csumoff;
module_param(csumoff, bool, 0644);
MODULE_PARM_DESC(csumoff, "OEM iNIC checksum-offload profile attribute");

static unsigned int max_fw_upload = RT3662_MAX_FW_UPLOADS;
module_param(max_fw_upload, uint, 0644);
MODULE_PARM_DESC(max_fw_upload, "Maximum OEM bootstrap attempts before requiring a cold reset");

static char *mode = "ap";
module_param(mode, charp, 0444);
MODULE_PARM_DESC(mode, "OEM compatibility mode; cfg80211 port supports AP");

static char *mac;
module_param(mac, charp, 0644);
MODULE_PARM_DESC(mac, "Optional OEM radio0 MAC override");

static char *mac2;
module_param(mac2, charp, 0644);
MODULE_PARM_DESC(mac2, "Optional OEM radio1 MAC override");

static const u8 rt3662_sl5501_fallback_mac[ETH_ALEN] = {
	0x1c, 0x74, 0x0d, 0xba, 0x28, 0x10,
};

/* OEM BootCmdHdr source. ACKs may be returned to this multicast address. */
static const u8 rt3662_boot_host_mc[ETH_ALEN] = {
	0x01, 0x0c, 0x43, 0x00, 0x00, 0x00,
};

#define RT3662_SL5501_ENV_SIZE 0x2000

static int rt3662_read_uboot_ethaddr(struct device *dev, u8 *mac)
{
	struct mtd_info *mtd;
	u8 *buf, *p, *end;
	size_t retlen = 0;
	int ret = -ENOENT;

	mtd = get_mtd_device_nm("uboot_cfg");
	if (IS_ERR(mtd))
		return PTR_ERR(mtd);

	buf = kmalloc(RT3662_SL5501_ENV_SIZE, GFP_KERNEL);
	if (!buf) {
		put_mtd_device(mtd);
		return -ENOMEM;
	}

	ret = mtd_read(mtd, 0, RT3662_SL5501_ENV_SIZE, &retlen, buf);
	put_mtd_device(mtd);
	if (ret || retlen != RT3662_SL5501_ENV_SIZE)
		goto out;

	/* Skip the 4-byte CRC and parse the NUL-separated U-Boot variables. */
	p = buf + 4;
	end = buf + RT3662_SL5501_ENV_SIZE;
	while (p < end && *p) {
		size_t left = end - p;
		size_t n = strnlen(p, left);

		if (n == left)
			break;
		if (n > 8 && !memcmp(p, "ethaddr=", 8) &&
		    mac_pton(p + 8, mac) && is_valid_ether_addr(mac)) {
			dev_info(dev, "using ethaddr %pM from uboot_cfg\n", mac);
			ret = 0;
			goto out;
		}
		p += n + 1;
	}
	ret = -ENOENT;

out:
	kfree(buf);
	return ret;
}

static void rt3662_mac_add(u8 *dst, const u8 *src, unsigned int add)
{
	int i;

	ether_addr_copy(dst, src);
	for (i = ETH_ALEN - 1; i >= 0 && add; i--) {
		unsigned int v = dst[i] + (add & 0xff);

		dst[i] = v & 0xff;
		add = (add >> 8) + (v >> 8);
	}
}

static void rt3662_init_oem_macs(struct rt3662_inic *inic)
{
	const u8 *base = ltq_get_eth_mac();

	/*
	 * Prefer the persistent U-Boot environment itself.  Appended-DTB MIPS
	 * kernels do not reliably inherit U-Boot's bootargs, so reading mtd1
	 * makes `setenv ethaddr ...; saveenv` authoritative for Linux as well.
	 * Fall back to the legacy kernel ethaddr parser, then to the MAC that was
	 * recovered from this unit's original OEM SPI dump.
	 */
	if (!rt3662_read_uboot_ethaddr(inic->dev, inic->mii_mac))
		;
	else if (base && is_valid_ether_addr(base))
		ether_addr_copy(inic->mii_mac, base);
	else
		ether_addr_copy(inic->mii_mac, rt3662_sl5501_fallback_mac);

	rt3662_mac_add(inic->radio[0].mac, inic->mii_mac, 0x0c);
	if (inic->num_radios > 1)
		rt3662_mac_add(inic->radio[1].mac, inic->mii_mac, 0x0e);
	if (mac && mac_pton(mac, inic->radio[0].mac) &&
	    !is_valid_ether_addr(inic->radio[0].mac))
		rt3662_mac_add(inic->radio[0].mac, inic->mii_mac, 0x0c);
	if (inic->num_radios > 1 && mac2 && mac_pton(mac2, inic->radio[1].mac) &&
	    !is_valid_ether_addr(inic->radio[1].mac))
		rt3662_mac_add(inic->radio[1].mac, inic->mii_mac, 0x0e);

	dev_info(inic->dev,
		 "OEM bootstrap identity: MiiMasterMac=%pM radio0=%pM radio1=%pM\n",
		 inic->mii_mac, inic->radio[0].mac,
		 inic->num_radios > 1 ? inic->radio[1].mac : inic->radio[0].mac);
}

static int rt3662_packet_rcv(struct sk_buff *skb, struct net_device *dev,
			     struct packet_type *pt, struct net_device *orig_dev)
{
	struct rt3662_inic *inic = container_of(pt, struct rt3662_inic, pt);

	if (skb->pkt_type == PACKET_OUTGOING || dev != inic->master) {
		kfree_skb(skb);
		return NET_RX_SUCCESS;
	}

	rt3662_handle_rx(inic, skb);
	return NET_RX_SUCCESS;
}

/*
 * Exact SL5501 OEM sequence from power_wlan_ralink.sh:
 *   assert reset -> power on -> 500 ms -> bring sw-p5-wifi up -> release reset.
 * The MII master is deliberately opened later by rt3662_enable_master().
 */
static int rt3662_power_prepare(struct rt3662_inic *inic)
{
	if (inic->reset_gpio)
		gpiod_set_value_cansleep(inic->reset_gpio, 1);
	if (inic->power_gpio)
		gpiod_set_value_cansleep(inic->power_gpio, 1);

	msleep(500);
	return 0;
}

static void rt3662_release_reset(struct rt3662_inic *inic)
{
	if (inic->reset_gpio)
		gpiod_set_value_cansleep(inic->reset_gpio, 0);
}

static void rt3662_power_off(struct rt3662_inic *inic)
{
	if (inic->reset_gpio)
		gpiod_set_value_cansleep(inic->reset_gpio, 1);
	if (inic->power_gpio)
		gpiod_set_value_cansleep(inic->power_gpio, 0);
}

/*
 * OEM RaCfgRestartiNIC() resets the RACFG state and, for the MII build,
 * explicitly prints "Please issue GPIO to Reset iNIC".  The older Ralink
 * source pulses the hardware reset for 10 ms.  Keep WLAN power and GSWIP P5
 * up during this recovery; only reset the iNIC core so it returns to
 * BOOT_NOTIFY without disturbing the rest of the xRX200 datapath.
 */
int rt3662_restart_inic_hw(struct rt3662_inic *inic)
{
	int ret;

	if (inic->shutting_down)
		return -ESHUTDOWN;
	if (!inic->reset_gpio) {
		dev_warn(inic->dev,
			 "OEM RACFG restart requested but no reset GPIO is available\n");
		return -ENODEV;
	}

	dev_warn(inic->dev,
		 "OEM RACFG recovery: pulsing iNIC reset GPIO for 10 ms\n");
	gpiod_set_value_cansleep(inic->reset_gpio, 1);
	usleep_range(10000, 12000);

	/* Re-assert the board-specific PCDU5 value before the boot ROM starts. */
	ret = rt3662_apply_port5_tx_delay(inic);
	if (ret)
		dev_err(inic->dev,
			"cannot restore GSWIP port5 TX delay during iNIC recovery: %d\n",
			ret);

	/* Never strand the radio in reset if the diagnostic/timing hook fails.
	 * The OEM recovery is a reset pulse, not a permanent reset assertion. */
	gpiod_set_value_cansleep(inic->reset_gpio, 0);
	return ret;
}

static int rt3662_resolve_master(struct rt3662_inic *inic,
				 struct platform_device *pdev)
{
	const char *name = miimaster;

	if (pdev->dev.of_node)
		of_property_read_string(pdev->dev.of_node, "ralink,mii-master", &name);

	inic->master = dev_get_by_name(&init_net, name);
	if (!inic->master) {
		dev_err(inic->dev, "MII master '%s' not found\n", name);
		return -EPROBE_DEFER;
	}

	dev_info(inic->dev, "resolved OEM MII master %s (%pM); keeping it down until 500-ms power delay completes\n",
		 inic->master->name, inic->master->dev_addr);
	return 0;
}

static int rt3662_enable_master(struct rt3662_inic *inic)
{
	struct sockaddr sa = { .sa_family = ARPHRD_ETHER };
	int ret = 0;

	/* Sphairon's MiiMasterMac comes from the MII master netdev when
	 * syncmiimac=0.  Give the DSA wifi port the persistent uboot_cfg base
	 * MAC before it is opened, rather than leaving OpenWrt's random MAC. */
	memcpy(sa.sa_data, inic->mii_mac, ETH_ALEN);
	rtnl_lock();
	ret = dev_set_mac_address(inic->master, &sa, NULL);
	rtnl_unlock();
	if (ret)
		dev_warn(inic->dev, "cannot set MII master MAC to %pM: %d\n",
			 inic->mii_mac, ret);

	if (!netif_running(inic->master)) {
		rtnl_lock();
		if (!netif_running(inic->master)) {
			ret = dev_open(inic->master, NULL);
			if (!ret)
				inic->master_opened = true;
		}
		rtnl_unlock();
		if (ret)
			return dev_err_probe(inic->dev, ret,
					     "cannot open OEM iNIC switch port %s\n",
					     inic->master->name);
	}

	/* Keep the pre-address bootstrap receive path exactly as the OEM MII host. */
	rtnl_lock();
	ret = dev_set_promiscuity(inic->master, 1);
	if (!ret)
		inic->master_promisc = true;
	if (!ret) {
		ret = dev_set_allmulti(inic->master, 1);
		if (!ret)
			inic->master_allmulti = true;
	}
	if (!ret) {
		ret = dev_mc_add(inic->master, rt3662_boot_host_mc);
		if (!ret)
			inic->boot_mc_added = true;
	}
	rtnl_unlock();
	if (ret)
		return dev_err_probe(inic->dev, ret,
				     "cannot configure OEM iNIC bootstrap receive path\n");

	inic->master_configured = true;
	dev_info(inic->dev,
		 "OEM power sequence: switch port %s UP after 500 ms; promisc=%u allmulti=%u bootstrap-mc=%u\n",
		 inic->master->name, inic->master_promisc,
		 inic->master_allmulti, inic->boot_mc_added);
	return 0;
}

static int rt3662_probe(struct platform_device *pdev)
{
	struct rt3662_inic *inic;
	int ret;

	inic = devm_kzalloc(&pdev->dev, sizeof(*inic), GFP_KERNEL);
	if (!inic)
		return -ENOMEM;

	inic->dev = &pdev->dev;
	inic->num_radios = clamp_t(unsigned int, radios, 1, RT3662_MAX_RADIOS);
	inic->bridge = bridge;
	inic->syncmiimac = syncmiimac;
	inic->csumoff = csumoff;
	inic->max_fw_upload = clamp_t(unsigned int, max_fw_upload, 1, 32);
	if (strcmp(mode, "ap"))
		return dev_err_probe(&pdev->dev, -EOPNOTSUPP,
				     "only OEM AP mode is supported by the cfg80211 port\n");
	mutex_init(&inic->cmd_lock);
	spin_lock_init(&inic->sync_lock);
	init_completion(&inic->sync_done);
	init_completion(&inic->fw_ready_done);
	rt3662_racfg_init_work(inic);
	platform_set_drvdata(pdev, inic);

	/* Optional DT GPIOs.  The SL5501 DTS supplies GPIO37 power/GPIO15 reset. */
	inic->power_gpio = devm_gpiod_get_optional(&pdev->dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(inic->power_gpio))
		return dev_err_probe(&pdev->dev, PTR_ERR(inic->power_gpio),
				     "failed to get WLAN power GPIO\n");

	inic->reset_gpio = devm_gpiod_get_optional(&pdev->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(inic->reset_gpio))
		return dev_err_probe(&pdev->dev, PTR_ERR(inic->reset_gpio),
				     "failed to get WLAN reset GPIO\n");

	ret = rt3662_resolve_master(inic, pdev);
	if (ret)
		return ret;

	rt3662_init_oem_macs(inic);

	ret = rt3662_cfg80211_register(inic);
	if (ret)
		goto err_master;

	inic->pt.type = cpu_to_be16(ETH_P_ALL);
	inic->pt.func = rt3662_packet_rcv;
	inic->pt.dev = inic->master;
	dev_add_pack(&inic->pt);

	ret = rt3662_power_prepare(inic);
	if (ret)
		goto err_pack;

	/* OEM: sw-p5-wifi is brought up only after the 500-ms powered-reset hold. */
	ret = rt3662_enable_master(inic);
	if (ret)
		goto err_power;

	/* Reproduce the legacy VR9 PCDU5 TX timing after phylink/dev_open has
	 * configured the RGMII port and before the iNIC is allowed to run. */
	ret = rt3662_apply_port5_tx_delay(inic);
	if (ret)
		goto err_power;

	/* OEM: release GPIO15 reset only after the MII/GSWIP path is ready. */
	rt3662_release_reset(inic);

	dev_info(&pdev->dev,
		 "RT3662 iNIC cfg80211 host registered; OEM reset released, waiting for BOOT_NOTIFY\n");
	return 0;

err_power:
	rt3662_power_off(inic);
err_pack:
	dev_remove_pack(&inic->pt);
	rt3662_cfg80211_unregister(inic);
err_master:
	if (inic->master) {
		rtnl_lock();
		if (inic->boot_mc_added) {
			dev_mc_del(inic->master, rt3662_boot_host_mc);
			inic->boot_mc_added = false;
		}
		if (inic->master_allmulti) {
			dev_set_allmulti(inic->master, -1);
			inic->master_allmulti = false;
		}
		if (inic->master_promisc) {
			dev_set_promiscuity(inic->master, -1);
			inic->master_promisc = false;
		}
		if (inic->master_opened && netif_running(inic->master)) {
			dev_close(inic->master);
			inic->master_opened = false;
		}
		rtnl_unlock();
	}
	dev_put(inic->master);
	inic->master = NULL;
	return ret;
}

static void rt3662_remove_common(struct platform_device *pdev)
{
	struct rt3662_inic *inic = platform_get_drvdata(pdev);

	inic->shutting_down = true;
	rt3662_heartbeat_stop(inic);
	cancel_work_sync(&inic->boot_work);
	dev_remove_pack(&inic->pt);
	rt3662_cfg80211_unregister(inic);
	rt3662_power_off(inic);
	if (inic->master) {
		rtnl_lock();
		if (inic->boot_mc_added) {
			dev_mc_del(inic->master, rt3662_boot_host_mc);
			inic->boot_mc_added = false;
		}
		if (inic->master_allmulti) {
			dev_set_allmulti(inic->master, -1);
			inic->master_allmulti = false;
		}
		if (inic->master_promisc) {
			dev_set_promiscuity(inic->master, -1);
			inic->master_promisc = false;
		}
		if (inic->master_opened && netif_running(inic->master)) {
			dev_close(inic->master);
			inic->master_opened = false;
		}
		rtnl_unlock();
		dev_put(inic->master);
		inic->master = NULL;
	}
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void rt3662_remove(struct platform_device *pdev)
{
	rt3662_remove_common(pdev);
}
#else
static int rt3662_remove(struct platform_device *pdev)
{
	rt3662_remove_common(pdev);
	return 0;
}
#endif

static const struct of_device_id rt3662_of_match[] = {
	{ .compatible = "ralink,rt3662-inic-mii" },
	{ .compatible = "sphairon,sl5501-rt3662-inic" },
	{ }
};
MODULE_DEVICE_TABLE(of, rt3662_of_match);

static struct platform_driver rt3662_driver = {
	.probe = rt3662_probe,
	.remove = rt3662_remove,
	.driver = {
		.name = RT3662_DRV_NAME,
		.of_match_table = rt3662_of_match,
	},
};
module_platform_driver(rt3662_driver);

MODULE_AUTHOR("OpenWrt SL5501 port");
MODULE_DESCRIPTION("Ralink RT3662/RT5392 iNIC MII cfg80211 driver for Speedlink 5501");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("ralink/rt3662/iNIC_ap.bin");
MODULE_FIRMWARE("ralink/rt3662/iNIC_ap.dat");
MODULE_FIRMWARE("ralink/rt3662/iNIC_ap1.dat");
MODULE_FIRMWARE("ralink/rt3662/iNIC_e2p.bin");
MODULE_FIRMWARE("ralink/rt3662/iNIC_e2p1.bin");
