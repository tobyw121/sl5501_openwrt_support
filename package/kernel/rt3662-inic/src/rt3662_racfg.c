// SPDX-License-Identifier: GPL-2.0-only
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/if_vlan.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/stdarg.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#include <linux/unaligned.h>
#else
#include <asm/unaligned.h>
#endif

#include "rt3662_inic.h"

#define RT3662_HEARTBEAT_CHECK_SEC 10
#define RT3662_HEARTBEAT_STALE_SEC 20
#define RT3662_HEARTBEAT_DISCOVERY_SEC 30

/* GPL-only diagnostics exported by the SL5501 GSWIP bring-up patch. */
extern int lantiq_gswip_sl5501_set_port5_tx_delay(unsigned int delay_ps);
extern int lantiq_gswip_sl5501_get_port5_tx_stats(u32 *tx_good,
                                                   u32 *tx_dropped);

static const unsigned int rt3662_sl5501_tx_delay_sweep[] = {
    2000, 1500, 2500, 1000, 3000, 500, 3500, 0,
};

/* Sphairon's VR9 switch initialization writes PCDU5=0x0003 for the external
 * RGMII port.  On xRX200 that is the TXDLY field value 3, i.e. 1500 ps.
 * BOOT_NOTIFY proves the iNIC->host direction is alive in the supplied log,
 * while every host->iNIC INITCFG times out.  Reproduce the legacy TX timing
 * before releasing the iNIC reset instead of relying on reset defaults. */
static int port5_tx_delay_ps = 1500;
module_param(port5_tx_delay_ps, int, 0644);
MODULE_PARM_DESC(port5_tx_delay_ps,
                 "SL5501 GSWIP port5 TX delay in ps (-1=leave unchanged, OEM=1500)");

/* Keep the wider sweep strictly opt-in for bring-up if 1500 ps still does not
 * produce an echo on a specific board. */
static bool tx_delay_sweep;
module_param(tx_delay_sweep, bool, 0644);
MODULE_PARM_DESC(tx_delay_sweep,
                 "Diagnostic only: sweep SL5501 GSWIP port5 TX delay during INITCFG seq0");

int rt3662_apply_port5_tx_delay(struct rt3662_inic *inic)
{
    int ret;

    if (port5_tx_delay_ps < 0) {
        dev_info(inic->dev, "leaving GSWIP port5 TX delay unchanged by request\n");
        return 0;
    }
    if (port5_tx_delay_ps > 3500 || port5_tx_delay_ps % 500)
        return dev_err_probe(inic->dev, -EINVAL,
                             "port5_tx_delay_ps must be -1 or 0..3500 in 500-ps steps\n");

    ret = lantiq_gswip_sl5501_set_port5_tx_delay(port5_tx_delay_ps);
    if (ret)
        return dev_err_probe(inic->dev, ret,
                             "cannot apply OEM GSWIP port5 TX delay\n");

    dev_info(inic->dev,
             "applied and hardware-verified OEM GSWIP port5 TX delay %d ps before iNIC reset release\n",
             port5_tx_delay_ps);
    return 0;
}

static atomic_t rt3662_cmdseq = ATOMIC_INIT(1);

static void rt3662_dump_boot_tx(struct rt3662_inic *inic, const u8 *eth,
				const struct rt3662_racfg_hdr *hdr, size_t len)
{
	/* One compact dump is enough to compare the on-wire OEM header without
	 * flooding UART during the ten retry attempts. */
	if (inic->boot_tx_dumped ||
	    le16_to_cpu(hdr->command_id) != RT3662_CMD_BOOT_INITCFG ||
	    le16_to_cpu(hdr->sequence) != 0)
		return;
	inic->boot_tx_dumped = true;

	dev_info(inic->dev,
		 "BOOT TX L2=%*phN RACFG=%*phN payload0=%*phN\n",
		 ETH_HLEN, eth, 36, (const u8 *)hdr,
		 (int)min_t(size_t, len, 16), hdr->data);
}

static const u8 rt3662_boot_ethhdr[ETH_HLEN] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x01, 0x0c, 0x43, 0x00, 0x00, 0x00,
	0xff, 0xff,
};

static int rt3662_status_to_errno(s32 status)
{
	switch (status) {
	case 0:  return 0;
	case -1: return -EFAULT;
	case -2: return -EOPNOTSUPP;
	case -3: return -EINVAL;
	case -4: return -ENETDOWN;
	case -5: return -EAGAIN;
	default: return status < 0 ? status : 0;
	}
}

static unsigned int rt3662_radio_from_devtype(struct rt3662_inic *inic, u16 dev_type)
{
	if ((dev_type & RT3662_DEV_TYPE_CONCURRENT) && inic->num_radios > 1)
		return 1;
	return 0;
}

int rt3662_racfg_send(struct rt3662_inic *inic, unsigned int radio,
		      u16 type, u16 id, u16 seq, u16 cmd_seq,
		      u16 dev_type, u16 dev_id, const void *data, size_t len)
{
	struct rt3662_racfg_hdr *hdr;
	struct sk_buff *skb;
	u8 *eth;
	int rc;

	if (!inic->master || len > RT3662_MAX_FEEDBACK)
		return -EINVAL;

	if (radio == 1)
		dev_type |= RT3662_DEV_TYPE_CONCURRENT;

	/* Keep the RACFG header 32-bit aligned exactly like the OEM MII path. */
	skb = alloc_skb(NET_SKB_PAD + 2 + ETH_HLEN + sizeof(*hdr) + len, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb, NET_SKB_PAD + 2);
	eth = skb_put(skb, ETH_HLEN);
	if (!inic->address_ready) {
		/* Exact BootCmdHdr from the Ralink MII host: never substitute the
		 * OpenWrt/DSA netdev source during bootstrap. */
		memcpy(eth, rt3662_boot_ethhdr, ETH_HLEN);
	} else {
		ether_addr_copy(eth, inic->radio[min_t(unsigned int, radio,
						     inic->num_radios - 1)].mac);
		ether_addr_copy(eth + ETH_ALEN, inic->mii_mac);
		put_unaligned_be16(RT3662_ETH_P_RACFG, eth + 12);
	}

	hdr = (void *)skb_put(skb, sizeof(*hdr) + len);
	memset(hdr, 0, sizeof(*hdr));
	hdr->magic_no = cpu_to_le32(RT3662_MAGIC);
	hdr->command_type = cpu_to_le16(type);
	hdr->command_id = cpu_to_le16(id);
	hdr->length = cpu_to_le16(len);
	hdr->sequence = cpu_to_le16(seq);
	hdr->dev_id = cpu_to_le16(dev_id);
	hdr->dev_type = cpu_to_le16(dev_type);
	hdr->command_seq = cpu_to_le16(cmd_seq);
	/*
	 * SL5501 is big-endian MIPS.  The vendor driver writes FLAG_BIG_ENDIAN
	 * as a native u16 (not cpu_to_le16), so the required wire bytes are
	 * literally 00 01.  Write those bytes explicitly to make the ABI
	 * independent of kernel preprocessor endian aliases.
	 */
	((u8 *)&hdr->flags)[0] = 0x00;
	((u8 *)&hdr->flags)[1] = RT3662_FLAG_BIG_ENDIAN;
	if (len)
		memcpy(hdr->data, data, len);

	if (!inic->address_ready)
		rt3662_dump_boot_tx(inic, eth, hdr, len);

	skb->dev = inic->master;
	/* OEM rlk_inic_mii_xmit() leaves skb->protocol untouched and only
	 * resets the network header before dev_queue_xmit().  Keep the same
	 * raw-L2 semantics; DSA adds/removes only its GSWIP PMAC tag.
	 */
	skb->ip_summed = CHECKSUM_NONE;
	skb_reset_network_header(skb);
	rc = dev_queue_xmit(skb);
	if (rc == NET_XMIT_DROP)
		return -ENOBUFS;
	return 0;
}

int rt3662_racfg_xfer(struct rt3662_inic *inic, unsigned int radio,
		      u16 type, u16 id, u16 seq, u16 cmd_seq,
		      u16 dev_type, u16 dev_id, const void *data, size_t len,
		      void *reply, size_t *reply_len, unsigned int timeout_ms)
{
	unsigned long flags;
	unsigned long timeout;
	int ret;

	mutex_lock(&inic->cmd_lock);
	reinit_completion(&inic->sync_done);

	spin_lock_irqsave(&inic->sync_lock, flags);
	memset(&inic->sync, 0, sizeof(inic->sync));
	inic->sync.armed = true;
	inic->sync.cmd_type = type;
	inic->sync.cmd_id = id;
	inic->sync.seq = seq;
	inic->sync.dev_type = dev_type | (radio ? RT3662_DEV_TYPE_CONCURRENT : 0);
	inic->sync.cmd_seq = cmd_seq;
	spin_unlock_irqrestore(&inic->sync_lock, flags);

	ret = rt3662_racfg_send(inic, radio, type, id, seq, cmd_seq,
				 dev_type, dev_id, data, len);
	if (ret)
		goto out_disarm;

	timeout = wait_for_completion_timeout(&inic->sync_done,
					       msecs_to_jiffies(timeout_ms));
	if (!timeout) {
		dev_err(inic->dev,
			"runtime RACFG timeout: radio=%u type=%04x id=%04x cmd_seq=%u after %u ms\n",
			radio, type, id, cmd_seq, timeout_ms);
		ret = -ETIMEDOUT;
		goto out_disarm;
	}

	spin_lock_irqsave(&inic->sync_lock, flags);
	ret = inic->sync.status;
	if (!ret && reply && reply_len) {
		size_t n = min(*reply_len, inic->sync.len);
		memcpy(reply, inic->sync.data, n);
		*reply_len = n;
	}
	inic->sync.armed = false;
	spin_unlock_irqrestore(&inic->sync_lock, flags);
	if (ret)
		dev_warn(inic->dev,
			 "runtime RACFG status error: radio=%u type=%04x id=%04x cmd_seq=%u ret=%d\n",
			 radio, type, id, cmd_seq, ret);
	mutex_unlock(&inic->cmd_lock);
	return ret;

out_disarm:
	spin_lock_irqsave(&inic->sync_lock, flags);
	inic->sync.armed = false;
	spin_unlock_irqrestore(&inic->sync_lock, flags);
	mutex_unlock(&inic->cmd_lock);
	return ret;
}

/*
 * The SL5501 2.7.0.5 host never transmits RACFG_CMD_GET_MAC here.
 * RaCfgSetUp() calls RaCfgWaitSyncRsp(GET_MAC, cmd_seq=0, 40s) after the
 * firmware BOOT_STARTUP.  GET_MAC is therefore a spontaneous synchronous
 * response from the iNIC.  Arm the waiter before STARTUP so a very fast
 * target cannot race past the host between dev_queue_xmit() and wait setup.
 */
static int rt3662_startup_wait_get_mac(struct rt3662_inic *inic,
				       void *reply, size_t *reply_len)
{
	unsigned long flags;
	unsigned long timeout;
	unsigned long started;
	int ret;

	mutex_lock(&inic->cmd_lock);
	reinit_completion(&inic->sync_done);

	spin_lock_irqsave(&inic->sync_lock, flags);
	memset(&inic->sync, 0, sizeof(inic->sync));
	inic->sync.armed = true;
	inic->sync.cmd_type = RT3662_CMD_TYPE_SYNC;
	inic->sync.cmd_id = RT3662_CMD_GET_MAC;
	inic->sync.seq = 0;
	inic->sync.dev_type = 0;
	inic->sync.cmd_seq = 0;
	spin_unlock_irqrestore(&inic->sync_lock, flags);

	started = jiffies;
	ret = rt3662_racfg_send(inic, 0, RT3662_CMD_TYPE_BOOTSTRAP,
			       RT3662_CMD_BOOT_STARTUP, 0, 0, 0, 0,
			       NULL, 0);
	if (ret)
		goto out_disarm;
	dev_info(inic->dev,
		 "OEM BOOT_STARTUP sent; waiting up to %u ms for spontaneous concurrent GET_MAC\n",
		 RT3662_GET_MAC_TIMEOUT_MS);

	timeout = wait_for_completion_timeout(&inic->sync_done,
				       msecs_to_jiffies(RT3662_GET_MAC_TIMEOUT_MS));
	if (!timeout) {
		ret = -ETIMEDOUT;
		goto out_disarm;
	}

	spin_lock_irqsave(&inic->sync_lock, flags);
	ret = inic->sync.status;
	if (!ret && reply && reply_len) {
		size_t n = min(*reply_len, inic->sync.len);

		memcpy(reply, inic->sync.data, n);
		*reply_len = n;
	}
	inic->sync.armed = false;
	spin_unlock_irqrestore(&inic->sync_lock, flags);
	if (!ret)
		dev_info(inic->dev,
			 "OEM GET_MAC received after %u ms, payload=%zu bytes\n",
			 jiffies_to_msecs(jiffies - started), inic->sync.len);
	mutex_unlock(&inic->cmd_lock);
	return ret;

out_disarm:
	spin_lock_irqsave(&inic->sync_lock, flags);
	inic->sync.armed = false;
	spin_unlock_irqrestore(&inic->sync_lock, flags);
	mutex_unlock(&inic->cmd_lock);
	return ret;
}

static size_t rt3662_iw_encode(u8 *buf, const struct iwreq *wrq,
			       enum rt3662_iw_type type,
			       const void *extra, size_t extra_len)
{
	u8 *p = buf;
	u16 n;

	switch (type) {
	case RT3662_IW_NAME:
		memcpy(p, wrq->u.name, IFNAMSIZ);
		p += IFNAMSIZ;
		break;
	case RT3662_IW_POINT:
		n = min_t(size_t, wrq->u.data.length, extra_len);
		put_unaligned(n, (u16 *)p); p += 2;
		put_unaligned(0, (u16 *)p); p += 2;
		if (n && extra) { memcpy(p, extra, n); p += n; }
		p = PTR_ALIGN(p, 2);
		put_unaligned(wrq->u.data.flags, (u16 *)p); p += 2;
		break;
	case RT3662_IW_PARAM:
		put_unaligned((s32)wrq->u.param.value, (s32 *)p); p += 4;
		*p++ = wrq->u.param.fixed;
		*p++ = wrq->u.param.disabled;
		put_unaligned(wrq->u.param.flags, (u16 *)p); p += 2;
		break;
	case RT3662_IW_FREQ:
		put_unaligned((s32)wrq->u.freq.m, (s32 *)p); p += 4;
		put_unaligned((s16)wrq->u.freq.e, (s16 *)p); p += 2;
		*p++ = wrq->u.freq.i;
		*p++ = wrq->u.freq.flags;
		break;
	case RT3662_IW_MODE:
		put_unaligned((u32)wrq->u.mode, (u32 *)p); p += 4;
		break;
	case RT3662_IW_SOCKADDR:
		put_unaligned((u16)wrq->u.addr.sa_family, (u16 *)p); p += 2;
		memcpy(p, wrq->u.addr.sa_data, 14); p += 14;
		break;
	case RT3662_IW_U32:
		memcpy(p, wrq, 8); p += 8;
		break;
	case RT3662_IW_QUALITY:
		memcpy(p, &wrq->u.qual, 4); p += 4;
		break;
	}
	return p - buf;
}

static void rt3662_iw_decode(struct iwreq *wrq, enum rt3662_iw_type type,
			     const u8 *buf, size_t len, void *extra, size_t extra_len)
{
	const u8 *p = buf;
	u16 n;

	if (!len)
		return;

	switch (type) {
	case RT3662_IW_NAME:
		if (len >= IFNAMSIZ)
			memcpy(wrq->u.name, p, IFNAMSIZ);
		break;
	case RT3662_IW_POINT:
		if (len < 6)
			break;
		n = get_unaligned((const u16 *)p); p += 4;
		wrq->u.data.length = n;
		if (extra && n)
			memcpy(extra, p, min_t(size_t, n, extra_len));
		p += min_t(size_t, n, len - 4);
		p = PTR_ALIGN(p, 2);
		if (p + 2 <= buf + len)
			wrq->u.data.flags = get_unaligned((const u16 *)p);
		break;
	case RT3662_IW_PARAM:
		if (len >= 8) {
			wrq->u.param.value = get_unaligned((const s32 *)p); p += 4;
			wrq->u.param.fixed = *p++;
			wrq->u.param.disabled = *p++;
			wrq->u.param.flags = get_unaligned((const u16 *)p);
		}
		break;
	case RT3662_IW_FREQ:
		if (len >= 8) {
			wrq->u.freq.m = get_unaligned((const s32 *)p); p += 4;
			wrq->u.freq.e = get_unaligned((const s16 *)p); p += 2;
			wrq->u.freq.i = *p++;
			wrq->u.freq.flags = *p;
		}
		break;
	default:
		break;
	}
}

int rt3662_iw_handler(struct rt3662_radio *radio, u16 cmd,
		      enum rt3662_iw_type type, struct iwreq *wrq,
		      void *extra, size_t extra_len)
{
	u8 *tx, *rx;
	size_t txlen, rxlen = RT3662_MAX_FEEDBACK + 64;
	u16 cmdseq = (u16)atomic_inc_return(&rt3662_cmdseq);
	int ret;

	if (!radio->inic->firmware_running)
		return -ENETDOWN;

	tx = kmalloc(RT3662_MAX_FEEDBACK + 64, GFP_KERNEL);
	rx = kmalloc(RT3662_MAX_FEEDBACK + 64, GFP_KERNEL);
	if (!tx || !rx) {
		ret = -ENOMEM;
		goto out;
	}

	txlen = rt3662_iw_encode(tx, wrq, type, extra, extra_len);
	ret = rt3662_racfg_xfer(radio->inic, radio->index,
				 RT3662_CMD_TYPE_IW_HANDLER, cmd, 0, cmdseq,
				 type, 0, tx, txlen, rx, &rxlen, 2000);
	if (!ret)
		rt3662_iw_decode(wrq, type, rx, rxlen, extra, extra_len);
	else
		dev_warn(radio->inic->dev,
			 "radio%u WEXT handler cmd=0x%04x type=%u failed: %d\n",
			 radio->index, cmd, type, ret);
out:
	kfree(tx);
	kfree(rx);
	return ret;
}

int rt3662_iw_private_set(struct rt3662_radio *radio, const char *fmt, ...)
{
	char str[512];
	u8 tx[RT3662_MAX_FEEDBACK + 64], rx[128];
	struct iwreq wrq = {};
	va_list ap;
	size_t txlen, rxlen = sizeof(rx);
	u16 cmdseq = (u16)atomic_inc_return(&rt3662_cmdseq);
	int n;

	va_start(ap, fmt);
	n = vscnprintf(str, sizeof(str), fmt, ap);
	va_end(ap);
	if (n <= 0)
		return -EINVAL;

	wrq.u.data.length = n;
	wrq.u.data.flags = 0;
	txlen = rt3662_iw_encode(tx, &wrq, RT3662_IW_POINT, str, n);
	n = rt3662_racfg_xfer(radio->inic, radio->index,
			       RT3662_CMD_TYPE_IWREQ_STRUC,
			       RT3662_RTPRIV_IOCTL_SET, 0, cmdseq,
			       RT3662_IW_POINT, 0, tx, txlen,
			       rx, &rxlen, 2000);
	if (n)
		dev_warn(radio->inic->dev,
			 "radio%u private SET '%s' failed: %d\n",
			 radio->index, str, n);
	return n;
}

int rt3662_tunnel_eapol(struct rt3662_radio *radio, const u8 *data, size_t len)
{
	struct rt3662_inic *inic = radio->inic;
	u16 cmdseq = ++radio->eapol_tx_cmdseq;
	u16 seq = 0;
	int ret = 0;

	while (len) {
		size_t chunk = min_t(size_t, len, RT3662_MAX_FEEDBACK);
		ret = rt3662_racfg_send(inic, radio->index, RT3662_CMD_TYPE_TUNNEL,
					 0, seq++, cmdseq, 0, 0, data, chunk);
		if (ret)
			return ret;
		data += chunk;
		len -= chunk;
	}
	return 0;
}

static void rt3662_rx_eapol(struct rt3662_inic *inic,
			    struct rt3662_racfg_hdr *hdr, size_t plen)
{
	unsigned int ridx = rt3662_radio_from_devtype(inic, le16_to_cpu(hdr->dev_type));
	struct rt3662_radio *radio = &inic->radio[ridx];
	u16 cmdseq = le16_to_cpu(hdr->command_seq);
	u16 seq = le16_to_cpu(hdr->sequence);

	if (!radio->ndev)
		return;
	/* In v20's profile-managed AP mode the iNIC owns the WPA2-PSK
	 * handshake using WPAPSK1 from iNIC_ap*.dat.  Do not inject a second
	 * copy into hostapd. */
	if (radio->profile_managed_ap || radio->native_profile_active)
		return;

	if (seq == 0 || cmdseq != radio->eapol_rx_cmdseq) {
		kfree_skb(radio->eapol_rx);
		radio->eapol_rx = alloc_skb(2304 + NET_IP_ALIGN, GFP_ATOMIC);
		if (!radio->eapol_rx)
			return;
		skb_reserve(radio->eapol_rx, NET_IP_ALIGN);
		radio->eapol_rx_cmdseq = cmdseq;
		radio->eapol_rx_seq = 0;
	}

	if (!radio->eapol_rx || seq != radio->eapol_rx_seq)
		return;
	if (skb_tailroom(radio->eapol_rx) < plen)
		return;
	memcpy(skb_put(radio->eapol_rx, plen), hdr->data, plen);
	radio->eapol_rx_seq++;

	if (plen < RT3662_MAX_FEEDBACK) {
		struct sk_buff *skb = radio->eapol_rx;
		radio->eapol_rx = NULL;
		skb->dev = radio->ndev;
		skb_reset_mac_header(skb);
		skb->protocol = eth_type_trans(skb, radio->ndev);
		netif_rx(skb);
	}
}

static void rt3662_rx_async(struct rt3662_inic *inic,
			    struct rt3662_racfg_hdr *hdr, size_t plen)
{
	u16 id = le16_to_cpu(hdr->command_id);
	u16 dev_type = le16_to_cpu(hdr->dev_type);
	unsigned int ridx = rt3662_radio_from_devtype(inic, dev_type);
	struct rt3662_radio *radio = &inic->radio[ridx];
	u32 event = le32_to_cpu(hdr->status);

	if (id == RT3662_CMD_HEART_BEAT) {
		WRITE_ONCE(inic->heartbeat_last_rx, jiffies);
		WRITE_ONCE(inic->heartbeat_rx_count,
			   READ_ONCE(inic->heartbeat_rx_count) + 1);
		if (!READ_ONCE(inic->heartbeat_seen)) {
			WRITE_ONCE(inic->heartbeat_seen, true);
			dev_info(inic->dev,
				 "OEM RACFG heartbeat stream detected (id=0x%04x)\n", id);
		}
		return;
	}

	if (id == RT3662_CMD_WIRELESS_SEND_EVENT2 && plen >= sizeof(union iwreq_data)) {
		union iwreq_data wrqu;
		memcpy(&wrqu, hdr->data, sizeof(wrqu));
		if (event == IWEVREGISTERED)
			rt3662_cfg80211_sta_event(radio, wrqu.addr.sa_data, true);
		else if (event == IWEVEXPIRED)
			rt3662_cfg80211_sta_event(radio, wrqu.addr.sa_data, false);
		return;
	}

	if (id == RT3662_CMD_WIRELESS_SEND_EVENT && plen >= ETH_ALEN) {
		u16 flags = le16_to_cpu(hdr->flags);
		if (flags == RT3662_ASSOC_EVENT_FLAG)
			rt3662_cfg80211_sta_event(radio, hdr->data, true);
		else if (flags == RT3662_DISASSOC_EVENT_FLAG)
			rt3662_cfg80211_sta_event(radio, hdr->data, false);
	}
}

static bool rt3662_try_complete(struct rt3662_inic *inic,
				struct rt3662_racfg_hdr *hdr, size_t plen)
{
	unsigned long flags;
	u16 type = le16_to_cpu(hdr->command_type);
	u16 id = le16_to_cpu(hdr->command_id);
	u16 seq = le16_to_cpu(hdr->sequence);
	u16 cmd_seq = le16_to_cpu(hdr->command_seq);
	bool handled = false;
	bool done = false;

	spin_lock_irqsave(&inic->sync_lock, flags);
	if (inic->sync.armed && inic->sync.cmd_id == id) {
		u16 expected = inic->sync.cmd_type & RT3662_CMD_TYPE_PASSIVE_MASK;
		u16 got = type & RT3662_CMD_TYPE_PASSIVE_MASK;

		if (expected == RT3662_CMD_TYPE_BOOTSTRAP) {
			/*
			 * The OEM RaCfgInitCfgAction/RaCfgUploadAction matches
			 * bootstrap ACKs by command id + packet sequence only.  In
			 * particular it does not reject an ACK because dev_type or
			 * command_seq changed.  Mirror that behaviour: concurrent
			 * firmware revisions are known to decorate bootstrap replies.
			 */
			if (got == expected && seq == inic->sync.seq) {
				handled = true;
				done = true;
				inic->sync.len = min_t(size_t, plen,
						       sizeof(inic->sync.data));
				memcpy(inic->sync.data, hdr->data, inic->sync.len);
				/* OEM bootstrap ACK actions ignore state/status completely. */
				inic->sync.status = 0;
				inic->sync.armed = false;
			}
		} else if ((type & RT3662_CMD_TYPE_RSP_FLAG) &&
			   cmd_seq == inic->sync.cmd_seq) {
			/*
			 * Runtime ioctl replies are multi-stage in the original
			 * rtXXXX_iNIC host driver.  IWREQ_STRUC/IW_HANDLER (and
			 * COPY_TO_USER) frames carry response data, but they do not
			 * finish the transaction.  RaCfgWaitSyncRsp() keeps waiting
			 * until RACFG_CMD_TYPE_IOCTL_STATUS (or SYNC) with the same
			 * command id + command_seq arrives.  v17 incorrectly required
			 * the response type to equal the request type, so it discarded
			 * that final status frame and every hostapd SET timed out after
			 * two seconds.
			 *
			 * Match the OEM implementation exactly here: command id and
			 * command_seq identify the runtime transaction.  dev_type and
			 * packet sequence are not part of RaCfgWaitSyncRsp() matching.
			 * OEM DispatchAdapter() deliberately routes concurrent replies
			 * before the waiter, so the waiter itself must not compare dev_type.
			 */
			if (got == expected || got == RT3662_CMD_TYPE_COPY_TO_USER) {
				size_t room = sizeof(inic->sync.data) - inic->sync.len;
				size_t n = min_t(size_t, plen, room);

				handled = true;
				if (n) {
					memcpy(inic->sync.data + inic->sync.len,
					       hdr->data, n);
					inic->sync.len += n;
				}

				/* A SYNC response is itself the final status/data frame. */
				if (got == RT3662_CMD_TYPE_SYNC) {
					inic->sync.status = rt3662_status_to_errno(
						(s32)le32_to_cpu(hdr->status));
					inic->sync.armed = false;
					done = true;
				}
			} else if (got == RT3662_CMD_TYPE_IOCTL_STATUS ||
				   got == RT3662_CMD_TYPE_SYNC) {
				handled = true;
				inic->sync.status = rt3662_status_to_errno(
					(s32)le32_to_cpu(hdr->status));
				/* SYNC may carry payload (notably GET_MAC). */
				if (got == RT3662_CMD_TYPE_SYNC && plen) {
					inic->sync.len = min_t(size_t, plen,
							       sizeof(inic->sync.data));
					memcpy(inic->sync.data, hdr->data,
					       inic->sync.len);
				}
				inic->sync.armed = false;
				done = true;
			}
		}
	}
	spin_unlock_irqrestore(&inic->sync_lock, flags);

	if (done)
		complete(&inic->sync_done);
	return handled;
}

void rt3662_handle_rx(struct rt3662_inic *inic, struct sk_buff *skb)
{
	struct rt3662_racfg_hdr *hdr;
	u16 type, id;
	size_t plen;

	if (skb->protocol != htons(RT3662_ETH_P_RACFG) || skb->len < sizeof(*hdr)) {
		kfree_skb(skb);
		return;
	}

	hdr = (void *)skb->data;
	if (le32_to_cpu(hdr->magic_no) != RT3662_MAGIC) {
		kfree_skb(skb);
		return;
	}

	plen = min_t(size_t, le16_to_cpu(hdr->length), skb->len - sizeof(*hdr));
	type = le16_to_cpu(hdr->command_type);
	id = le16_to_cpu(hdr->command_id);

	/* OEM MII syncmiimac=0 L2 filter after GET_MAC. */
	if (inic->address_ready) {
		const struct ethhdr *eh = eth_hdr(skb);
		unsigned int ridx = rt3662_radio_from_devtype(inic,
				le16_to_cpu(hdr->dev_type));

		if (!ether_addr_equal(eh->h_dest, inic->mii_mac) ||
		    !ether_addr_equal(eh->h_source, inic->radio[ridx].mac)) {
			dev_dbg_ratelimited(inic->dev,
				"drop RACFG L2 mismatch: %pM->%pM expected %pM->%pM\n",
				eh->h_source, eh->h_dest, inic->radio[ridx].mac,
				inic->mii_mac);
			kfree_skb(skb);
			return;
		}
	}

	if ((type & RT3662_CMD_TYPE_PASSIVE_MASK) == RT3662_CMD_TYPE_BOOTSTRAP &&
	    (id != RT3662_CMD_BOOT_UPLOAD || le16_to_cpu(hdr->sequence) < 3)) {
		/* The RT3662 boot ROM only initializes the bootstrap fields needed
		 * by RaCfgInitCfgAction/RaCfgUploadAction.  dev_type, flags and
		 * state/status contain boot-ROM scratch values on the SL5501 and are
		 * deliberately not printed as protocol state.  Successful byte-for-
		 * byte payload echoes below prove the header/payload alignment. */
		dev_info(inic->dev,
			 "OEM bootstrap RX: type=%04x id=%04x seq=%u len=%zu\n",
			 type, id, le16_to_cpu(hdr->sequence), plen);
	}

	if ((type & RT3662_CMD_TYPE_PASSIVE_MASK) == RT3662_CMD_TYPE_BOOTSTRAP &&
	    id == RT3662_CMD_BOOT_NOTIFY) {
		inic->boot_seen = true;
		rt3662_schedule_boot(inic);
		kfree_skb(skb);
		return;
	}

	if (rt3662_try_complete(inic, hdr, plen)) {
		kfree_skb(skb);
		return;
	}

	if ((type & RT3662_CMD_TYPE_PASSIVE_MASK) == RT3662_CMD_TYPE_TUNNEL) {
		rt3662_rx_eapol(inic, hdr, plen);
		kfree_skb(skb);
		return;
	}

	if ((type & RT3662_CMD_TYPE_PASSIVE_MASK) == RT3662_CMD_TYPE_ASYNC) {
		rt3662_rx_async(inic, hdr, plen);
		kfree_skb(skb);
		return;
	}

	kfree_skb(skb);
}

static void rt3662_abort_runtime_waiter(struct rt3662_inic *inic, int status)
{
	unsigned long flags;
	bool wake = false;

	spin_lock_irqsave(&inic->sync_lock, flags);
	if (inic->sync.armed) {
		inic->sync.status = status;
		inic->sync.armed = false;
		wake = true;
	}
	spin_unlock_irqrestore(&inic->sync_lock, flags);
	if (wake)
		complete(&inic->sync_done);
}

static void rt3662_heartbeat_worker(struct work_struct *work)
{
	struct rt3662_inic *inic = container_of(to_delayed_work(work),
						 struct rt3662_inic, heartbeat_work);
	unsigned long last, started;
	unsigned int i;
	int ret;

	if (inic->shutting_down)
		return;

	if (!inic->firmware_running || inic->boot_in_progress)
		goto rearm;

	started = READ_ONCE(inic->heartbeat_monitor_started);
	if (!READ_ONCE(inic->heartbeat_seen)) {
		/* Some firmware revisions may omit the optional heartbeat feedback.
		 * Do not create a reset loop merely because no stream was ever seen. */
		if (!inic->heartbeat_warned_absent && started &&
		    time_after(jiffies, started + RT3662_HEARTBEAT_DISCOVERY_SEC * HZ)) {
			inic->heartbeat_warned_absent = true;
			dev_warn(inic->dev,
				 "OEM RACFG heartbeat not observed in %u s; watchdog remains passive\n",
				 RT3662_HEARTBEAT_DISCOVERY_SEC);
		}
		goto rearm;
	}

	last = READ_ONCE(inic->heartbeat_last_rx);
	if (last && time_after(jiffies, last + RT3662_HEARTBEAT_STALE_SEC * HZ)) {
		if (inic->heartbeat_recovering)
			goto rearm;
		inic->heartbeat_recovering = true;
		dev_err(inic->dev,
			"OEM RACFG heartbeat stale for >%u s; performing OEM-style iNIC GPIO recovery\n",
			RT3662_HEARTBEAT_STALE_SEC);

		/* Serialize against any RACFG ioctl transaction before resetting the
		 * target.  If userspace is currently inside a short transaction, retry
		 * the watchdog on the next tick rather than resetting under it. */
		if (!mutex_trylock(&inic->cmd_lock)) {
			inic->heartbeat_recovering = false;
			goto rearm;
		}
		rt3662_abort_runtime_waiter(inic, -ENETRESET);
		inic->firmware_running = false;
		inic->address_ready = false;
		inic->boot_seen = false;
		inic->boot_tx_dumped = false;
		reinit_completion(&inic->fw_ready_done);
		for (i = 0; i < inic->num_radios; i++) {
			inic->radio[i].native_profile_active = false;
			if (inic->radio[i].ndev)
				netif_carrier_off(inic->radio[i].ndev);
		}
		ret = rt3662_restart_inic_hw(inic);
		mutex_unlock(&inic->cmd_lock);
		if (ret)
			dev_err(inic->dev, "heartbeat GPIO recovery failed: %d\n", ret);
		WRITE_ONCE(inic->heartbeat_seen, false);
		WRITE_ONCE(inic->heartbeat_last_rx, jiffies);
		WRITE_ONCE(inic->heartbeat_monitor_started, jiffies);
		inic->heartbeat_warned_absent = false;
		inic->heartbeat_recovering = false;
		goto rearm;
	}

	if (++inic->heartbeat_report_ticks >= 6) {
		inic->heartbeat_report_ticks = 0;
		dev_info(inic->dev,
			 "OEM RACFG heartbeat healthy: rx=%u last=%u ms ago\n",
			 READ_ONCE(inic->heartbeat_rx_count),
			 jiffies_to_msecs(jiffies - last));
	}

rearm:
	if (!inic->shutting_down)
		schedule_delayed_work(&inic->heartbeat_work,
				      RT3662_HEARTBEAT_CHECK_SEC * HZ);
}

void rt3662_heartbeat_fw_ready(struct rt3662_inic *inic)
{
	WRITE_ONCE(inic->heartbeat_monitor_started, jiffies);
	WRITE_ONCE(inic->heartbeat_last_rx, jiffies);
	WRITE_ONCE(inic->heartbeat_seen, false);
	inic->heartbeat_warned_absent = false;
	inic->heartbeat_report_ticks = 0;
	mod_delayed_work(system_wq, &inic->heartbeat_work,
			 RT3662_HEARTBEAT_CHECK_SEC * HZ);
}

void rt3662_heartbeat_stop(struct rt3662_inic *inic)
{
	cancel_delayed_work_sync(&inic->heartbeat_work);
}

static int rt3662_upload_stream(struct rt3662_inic *inic, u16 command,
                                const u8 *data, size_t len)
{
	u16 seq = 0;

	while (len) {
		size_t chunk = min_t(size_t, len, RT3662_MAX_FEEDBACK);
		unsigned int attempt;
		int ret = -ETIMEDOUT;

		if (command == RT3662_CMD_BOOT_INITCFG || seq < 3)
			dev_info(inic->dev,
				 "OEM bootstrap TX: cmd=%u seq=%u len=%zu source=%pM flags-wire=00:01\n",
				 command, seq, chunk, rt3662_boot_ethhdr + ETH_ALEN);

		for (attempt = 0; attempt < RT3662_BOOT_TX_ATTEMPTS; attempt++) {
			u8 echo[RT3662_MAX_FEEDBACK];
			size_t echo_len = sizeof(echo);
			u32 tx_good_before = 0, tx_drop_before = 0;
			u32 tx_good_after = 0, tx_drop_after = 0;
			bool have_rmon = false;

			/* Record a hardware TX counter delta for the first INITCFG even in
			 * production mode.  If the echo still times out, the next bootlog
			 * immediately tells us whether GSWIP actually transmitted it. */
			if (command == RT3662_CMD_BOOT_INITCFG && seq == 0 &&
			    (attempt == 0 || tx_delay_sweep) &&
			    !lantiq_gswip_sl5501_get_port5_tx_stats(&tx_good_before,
			                                             &tx_drop_before))
				have_rmon = true;

			/* The old host does not sweep PCDU.  This remains an explicit
			 * diagnostic fallback only. */
			if (tx_delay_sweep && command == RT3662_CMD_BOOT_INITCFG &&
			    seq == 0 && attempt < ARRAY_SIZE(rt3662_sl5501_tx_delay_sweep)) {
				unsigned int delay_ps = rt3662_sl5501_tx_delay_sweep[attempt];
				int dret = lantiq_gswip_sl5501_set_port5_tx_delay(delay_ps);

				if (dret)
					dev_warn(inic->dev,
						 "cannot set GSWIP port5 TX delay %u ps: %d\n",
						 delay_ps, dret);
				else
					dev_info(inic->dev,
						 "diagnostic: GSWIP port5 TX delay %u ps (%u/%zu)\n",
						 delay_ps, attempt + 1,
						 ARRAY_SIZE(rt3662_sl5501_tx_delay_sweep));
				usleep_range(2000, 4000);
			}

			ret = rt3662_racfg_xfer(inic, 0, RT3662_CMD_TYPE_BOOTSTRAP,
						 command, seq, 0, 0, 0,
						 data, chunk, echo, &echo_len,
						 RT3662_BOOT_TIMEOUT_MS);

			if (have_rmon &&
			    !lantiq_gswip_sl5501_get_port5_tx_stats(&tx_good_after,
									      &tx_drop_after))
				dev_info(inic->dev,
					 "diagnostic GSWIP port5 TX RMON: good %u->%u (+%u), dropped %u->%u (+%u)\n",
					 tx_good_before, tx_good_after,
					 tx_good_after - tx_good_before,
					 tx_drop_before, tx_drop_after,
					 tx_drop_after - tx_drop_before);

			if (!ret) {
				/* RaCfgInitCfgAction()/RaCfgUploadAction() compare the
				 * echoed payload byte-for-byte before advancing. */
				if (echo_len != chunk || memcmp(echo, data, chunk)) {
					dev_err(inic->dev,
						 "OEM bootstrap echo mismatch: cmd=%u seq=%u sent=%zu got=%zu\n",
						 command, seq, chunk, echo_len);
					return -EBADMSG;
				}
				break;
			}

			if (ret != -ETIMEDOUT)
				return ret;
			dev_warn(inic->dev,
				 "OEM bootstrap ACK timeout: cmd=%u seq=%u tx=%u/%u (initial + %u retries)\n",
				 command, seq, attempt + 1, RT3662_BOOT_TX_ATTEMPTS,
				 RT3662_BOOT_RETRIES);
		}
		if (ret) {
			if (tx_delay_sweep && command == RT3662_CMD_BOOT_INITCFG && seq == 0 &&
			    port5_tx_delay_ps >= 0)
				lantiq_gswip_sl5501_set_port5_tx_delay(port5_tx_delay_ps);
			return ret;
		}

		data += chunk;
		len -= chunk;
		seq++;
		/* rtXXXX_iNIC 2.7.0.5 advances immediately after a valid
		 * RaCfgUploadAction echo. Older generic Ralink sources used a
		 * 30-ms sleep, but the SL5501 OEM module does not. */
	}
	dev_info(inic->dev, "OEM bootstrap upload complete: cmd=%u packets=%u\n",
		 command, seq);
	return 0;
}

static int rt3662_load_profile(struct rt3662_inic *inic, const char *name,
			       const char *fallback, u8 **out, size_t *out_len)
{
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, name, inic->dev);
	if (!ret) {
		*out = kmemdup(fw->data, fw->size, GFP_KERNEL);
		*out_len = fw->size;
		release_firmware(fw);
		return *out ? 0 : -ENOMEM;
	}

	dev_warn(inic->dev, "%s unavailable, using conservative built-in profile\n", name);
	*out_len = strlen(fallback);
	*out = kmemdup(fallback, *out_len, GFP_KERNEL);
	return *out ? 0 : -ENOMEM;
}

static const char rt3662_profile_5g[] =
	"Default\nCountryRegion=1\nCountryRegionABand=1\nCountryCode=DE\nBssidNum=1\n"
	"SSID1=openwrt_5GHz\nWirelessMode=11\nChannel=40\nBasicRate=15\nBeaconPeriod=100\n"
	"DtimPeriod=1\nTxPower=100\nWmmCapable=1\nHideSSID=0\nAutoChannelSelect=0\n"
	"IEEE80211H=0\nCSPeriod=0\nFastDfs=0\nChMovTime=0\n"
	"WirelessEvent=1\nIEEE8021X=0\nAuthMode=WPA2PSK\nEncrypType=AES\n"
	"RekeyInterval=0\nRekeyMethod=DISABLE\nPMKCachePeriod=10\n"
	""
	"session_timeout_interval=3600\nWPAPSK1=12345678!\nDefaultKeyID=1\nHT_DisallowTKIP=1\n"
	"HT_BW=0\nHT_EXTCHA=1\nHT_AutoBA=1\nHT_AMSDU=0\nHT_BAWinSize=64\n"
	"HT_GI=1\nHT_MCS=33\nHT_TxStream=2\nHT_RxStream=2\nExtEEPROM=0\nDBG_LEVEL=0\n";

static const char rt3662_profile_2g[] =
	"Default\nCountryRegion=1\nCountryRegionABand=1\nCountryCode=DE\nBssidNum=1\n"
	"SSID1=openwrt_2,4GHz\nWirelessMode=9\nChannel=1\nBasicRate=15\nBeaconPeriod=100\n"
	"DtimPeriod=1\nTxPower=100\nWmmCapable=1\nHideSSID=0\nAutoChannelSelect=0\n"
	"WirelessEvent=1\nIEEE8021X=0\nAuthMode=WPA2PSK\nEncrypType=AES\n"
	"RekeyInterval=0\nRekeyMethod=DISABLE\nPMKCachePeriod=10\n"
	""
	"session_timeout_interval=3600\nWPAPSK1=12345678!\nDefaultKeyID=1\nHT_DisallowTKIP=1\n"
	"HT_BW=0\nHT_EXTCHA=1\nHT_AutoBA=1\nHT_AMSDU=0\nHT_BAWinSize=64\n"
	"HT_GI=1\nHT_MCS=33\nHT_TxStream=2\nHT_RxStream=2\nExtEEPROM=0\nDBG_LEVEL=0\n";

static bool rt3662_profile_ext_eeprom(const u8 *data, size_t len)
{
	static const char key[] = "ExtEEPROM=1";
	size_t i;

	if (len < sizeof(key) - 1)
		return false;
	for (i = 0; i + sizeof(key) - 1 <= len; i++)
		if (!memcmp(data + i, key, sizeof(key) - 1) &&
		    (i == 0 || data[i - 1] == '\n' || data[i - 1] == '\r'))
			return true;
	return false;
}

static int rt3662_load_e2p(struct rt3662_inic *inic, const char *name,
			   u8 **out, size_t *out_len)
{
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, name, inic->dev);
	if (ret)
		return ret;
	if (!fw->size || fw->size > RT3662_MAX_EXT_EEPROM) {
		release_firmware(fw);
		return -EINVAL;
	}
	*out = kmemdup(fw->data, fw->size, GFP_KERNEL);
	*out_len = fw->size;
	release_firmware(fw);
	return *out ? 0 : -ENOMEM;
}

static int rt3662_build_profiles(struct rt3662_inic *inic, u8 **out, size_t *out_len)
{
	u8 *p0 = NULL, *p1 = NULL, *e2p0 = NULL, *e2p1 = NULL, *buf;
	size_t l0 = 0, l1 = 0, e2p0_len = 0, e2p1_len = 0, cap, n = 0;
	char extra0[384], extra1[192];
	bool ext0, ext1;
	int ret, e0, e1;

	ret = rt3662_load_profile(inic, "ralink/rt3662/iNIC_ap.dat",
				  rt3662_profile_5g, &p0, &l0);
	if (ret)
		return ret;
	ret = rt3662_load_profile(inic, "ralink/rt3662/iNIC_ap1.dat",
				  rt3662_profile_2g, &p1, &l1);
	if (ret)
		goto out;

	ext0 = rt3662_profile_ext_eeprom(p0, l0);
	ext1 = rt3662_profile_ext_eeprom(p1, l1);
	if (ext0) {
		ret = rt3662_load_e2p(inic, "ralink/rt3662/iNIC_e2p.bin",
				       &e2p0, &e2p0_len);
		if (ret) {
			dev_err(inic->dev, "RT3662 profile requests external EEPROM but OEM E2P cannot be loaded: %d\n", ret);
			goto out;
		}
	}
	if (ext1 && inic->num_radios > 1) {
		ret = rt3662_load_e2p(inic, "ralink/rt3662/iNIC_e2p1.bin",
				       &e2p1, &e2p1_len);
		if (ret) {
			dev_err(inic->dev, "RT5392 profile requests external EEPROM but OEM E2P cannot be loaded: %d\n", ret);
			goto out;
		}
	}

	/* Exact _append_extra_profile() policy for concurrent MII AP mode. */
	e0 = scnprintf(extra0, sizeof(extra0), "\n");
	if (inic->syncmiimac)
		e0 += scnprintf(extra0 + e0, sizeof(extra0) - e0,
				"__SkipIfNotINIC__MAC=%pM\n", inic->master->dev_addr);
	else {
		e0 += scnprintf(extra0 + e0, sizeof(extra0) - e0,
				"__SkipIfNotINIC__MAC=%pM\n", inic->radio[0].mac);
		e0 += scnprintf(extra0 + e0, sizeof(extra0) - e0,
				"MiiMasterMac=%pM\n", inic->mii_mac);
	}
	e0 += scnprintf(extra0 + e0, sizeof(extra0) - e0,
			"__SkipIfNotINIC__BUILT_IN_BRIDGE=%u\n", inic->bridge);
	if (inic->csumoff)
		e0 += scnprintf(extra0 + e0, sizeof(extra0) - e0,
				"__SkipIfNotINIC__CSUM_OFFLOAD=1\n");
	e0 += scnprintf(extra0 + e0, sizeof(extra0) - e0,
			"__SkipIfNotINIC__CONCURRENT_ENABLE=1\n");
#ifdef __BIG_ENDIAN
	e0 += scnprintf(extra0 + e0, sizeof(extra0) - e0,
			"__SkipIfNotINIC__BIG_ENDIAN=1\n");
#endif
	if (ext0)
		e0 += scnprintf(extra0 + e0, sizeof(extra0) - e0,
				"__SkipIfNotINIC__EXT_EEPROM_SIZE=%zu\n", e2p0_len);

	e1 = scnprintf(extra1, sizeof(extra1),
		"\n__SkipIfNotINIC__MAC=%pM\n",
		inic->syncmiimac ? inic->master->dev_addr : inic->radio[1].mac);
	if (ext1)
		e1 += scnprintf(extra1 + e1, sizeof(extra1) - e1,
				"__SkipIfNotINIC__EXT_EEPROM_SIZE=%zu\n", e2p1_len);

	/* Conceptual stream produced by OEM _upload_profile(): profile0,
	 * extra0, second-profile separator, profile1, extra1, NUL, then the
	 * external E2P binaries (only when each profile says ExtEEPROM=1). */
	cap = l0 + e0 + 1 + l1 + e1 + 1 + e2p0_len + e2p1_len;
	buf = kmalloc(cap, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(buf + n, p0, l0); n += l0;
	memcpy(buf + n, extra0, e0); n += e0;
	if (inic->num_radios > 1) {
		buf[n++] = '\n';
		memcpy(buf + n, p1, l1); n += l1;
		memcpy(buf + n, extra1, e1); n += e1;
	}
	buf[n++] = '\0';
	if (e2p0_len) { memcpy(buf + n, e2p0, e2p0_len); n += e2p0_len; }
	if (e2p1_len) { memcpy(buf + n, e2p1, e2p1_len); n += e2p1_len; }

	*out = buf;
	*out_len = n;
	dev_info(inic->dev,
		 "OEM INITCFG stream: %zu bytes, %zu packets (profile0=%zu profile1=%zu ext-e2p=%zu/%zu)\n",
		 n, DIV_ROUND_UP(n, RT3662_MAX_FEEDBACK), l0, l1,
		 e2p0_len, e2p1_len);
	ret = 0;
out:
	kfree(p0);
	kfree(p1);
	kfree(e2p0);
	kfree(e2p1);
	return ret;
}

static int rt3662_validate_firmware(struct rt3662_inic *inic,
				    const struct firmware *fw)
{
	static const u8 trailer_magic[4] = { 0x01, 0x08, 0x14, 0x18 };
	const u8 *trailer;
	u32 declared, stored_crc, payload_crc;
	size_t payload_len;

	if (fw->size < RT3662_CRC_HEADER_LEN)
		return -EINVAL;
	payload_len = fw->size - RT3662_CRC_HEADER_LEN;
	trailer = fw->data + payload_len;
	if (memcmp(trailer, trailer_magic, sizeof(trailer_magic))) {
		dev_err(inic->dev, "iNIC firmware has no OEM RT3662 CRC record\n");
		return -EINVAL;
	}

	stored_crc = get_unaligned_le32(trailer + 4);
	declared = get_unaligned_le32(trailer + RT3662_CRC_HEADER_LEN - 4);
	if (declared != payload_len) {
		dev_err(inic->dev,
			"iNIC firmware CRC record size %u != payload %zu (file %zu)\n",
			declared, payload_len, fw->size);
		return -EINVAL;
	}

	/* OEM crc32.c is zlib CRC-32.  Linux crc32() needs explicit zlib
	 * pre/post conditioning to reproduce it. */
	payload_crc = crc32(~0U, fw->data, payload_len) ^ ~0U;
	if (payload_crc != stored_crc) {
		dev_err(inic->dev,
			"iNIC firmware payload CRC %08x != OEM record %08x\n",
			payload_crc, stored_crc);
		return -EBADMSG;
	}

	dev_info(inic->dev,
		 "validated OEM iNIC firmware: version=%.12s date=%.16s payload=%u trailer=%u file=%zu crc=%08x\n",
		 trailer + 8, trailer + 20, declared, RT3662_CRC_HEADER_LEN,
		 fw->size, stored_crc);
	return 0;
}

static int rt3662_apply_get_mac_reply(struct rt3662_inic *inic,
                                      const u8 *reply, size_t reply_len)
{
	u8 mac0[ETH_ALEN], mac1[ETH_ALEN] = {};
	u8 old0[ETH_ALEN], old1[ETH_ALEN] = {};
	char text[18];

	/* RaCfgWaitSyncRsp(): 18 bytes for one MAC, 35 for two concurrent MACs. */
	if (inic->num_radios > 1) {
		if (reply_len != 35)
			return -EPROTO;
	} else if (reply_len != 18) {
		return -EPROTO;
	}

	memcpy(text, reply, 17);
	text[17] = '\0';
	if (!mac_pton(text, mac0) || !is_valid_ether_addr(mac0))
		return -EBADMSG;
	if (inic->num_radios > 1) {
		memcpy(text, reply + 17, 17);
		text[17] = '\0';
		if (!mac_pton(text, mac1) || !is_valid_ether_addr(mac1))
			return -EBADMSG;
	}

	ether_addr_copy(old0, inic->radio[0].mac);
	if (inic->num_radios > 1)
		ether_addr_copy(old1, inic->radio[1].mac);
	ether_addr_copy(inic->radio[0].mac, mac0);
	if (inic->num_radios > 1)
		ether_addr_copy(inic->radio[1].mac, mac1);

	if (!ether_addr_equal(old0, inic->radio[0].mac) ||
	    (inic->num_radios > 1 &&
	     !ether_addr_equal(old1, inic->radio[1].mac)))
		dev_warn(inic->dev,
			 "GET_MAC changed target identity: radio0 %pM->%pM radio1 %pM->%pM\n",
			 old0, inic->radio[0].mac,
			 inic->num_radios > 1 ? old1 : old0,
			 inic->num_radios > 1 ? inic->radio[1].mac : inic->radio[0].mac);

	if (inic->radio[0].wiphy)
		ether_addr_copy(inic->radio[0].wiphy->perm_addr, inic->radio[0].mac);
	if (inic->radio[0].ndev)
		eth_hw_addr_set(inic->radio[0].ndev, inic->radio[0].mac);
	if (inic->num_radios > 1) {
		if (inic->radio[1].wiphy)
			ether_addr_copy(inic->radio[1].wiphy->perm_addr, inic->radio[1].mac);
		if (inic->radio[1].ndev)
			eth_hw_addr_set(inic->radio[1].ndev, inic->radio[1].mac);
	}
	return 0;
}

static void rt3662_boot_worker(struct work_struct *work)
{
	struct rt3662_inic *inic = container_of(work, struct rt3662_inic, boot_work);
	const struct firmware *fw = NULL;
	u8 *profile = NULL;
	size_t profile_len = 0, reply_len;
	u8 reply[128];
	bool restart_hw = false;
	int ret;

	if (inic->shutting_down)
		return;
	if (inic->boot_attempts >= inic->max_fw_upload) {
		dev_err(inic->dev,
			"OEM bootstrap limit reached (%u attempts); cold reset required before retry\n",
			inic->max_fw_upload);
		return;
	}
	inic->boot_attempts++;
	dev_info(inic->dev, "OEM bootstrap attempt %u/%u (host driver reference 2.7.0.5)\n",
		 inic->boot_attempts, inic->max_fw_upload);
	inic->boot_in_progress = true;
	inic->firmware_running = false;
	inic->address_ready = false;
	inic->boot_tx_dumped = false;
	reinit_completion(&inic->fw_ready_done);

	ret = request_firmware(&fw, "ralink/rt3662/iNIC_ap.bin", inic->dev);
	if (ret) {
		dev_err(inic->dev, "cannot load SL5501 iNIC firmware: %d\n", ret);
		goto out;
	}
	ret = rt3662_validate_firmware(inic, fw);
	if (ret)
		goto out;
	ret = rt3662_build_profiles(inic, &profile, &profile_len);
	if (ret)
		goto out;

	dev_info(inic->dev, "BOOT_NOTIFY: uploading %zu-byte concurrent profile in %zu packets\n",
		 profile_len, DIV_ROUND_UP(profile_len, RT3662_MAX_FEEDBACK));
	ret = rt3662_upload_stream(inic, RT3662_CMD_BOOT_INITCFG, profile, profile_len);
	if (ret) {
		dev_err(inic->dev, "profile upload failed: %d\n", ret);
		restart_hw = true;
		goto out;
	}

	dev_info(inic->dev, "uploading iNIC firmware (%zu bytes)\n", fw->size);
	ret = rt3662_upload_stream(inic, RT3662_CMD_BOOT_UPLOAD, fw->data, fw->size);
	if (ret) {
		dev_err(inic->dev, "firmware upload failed: %d\n", ret);
		restart_hw = true;
		goto out;
	}

	/* OEM _upload_firmware() emits BOOT_STARTUP, then RaCfgSetUp() waits
	 * passively for spontaneous GET_MAC(cmd_seq=0).  Arm first to avoid a
	 * STARTUP->GET_MAC race; do not transmit a made-up GET_MAC request. */
	reply_len = sizeof(reply) - 1;
	ret = rt3662_startup_wait_get_mac(inic, reply, &reply_len);
	if (ret) {
		dev_err(inic->dev,
			"OEM BOOT_STARTUP/GET_MAC confirmation failed: %d\n", ret);
		restart_hw = true;
		goto out;
	}
	ret = rt3662_apply_get_mac_reply(inic, reply, reply_len);
	if (ret) {
		dev_err(inic->dev, "invalid OEM GET_MAC reply: len=%zu error=%d\n",
			reply_len, ret);
		restart_hw = true;
		goto out;
	}

	inic->address_ready = true;
	inic->firmware_running = true;
	inic->boot_attempts = 0;
	rt3662_cfg80211_fw_ready(inic);
	complete_all(&inic->fw_ready_done);
	dev_info(inic->dev, "RT3662 iNIC firmware running: radio0=%pM radio1=%pM\n",
		 inic->radio[0].mac,
		 inic->num_radios > 1 ? inic->radio[1].mac : inic->radio[0].mac);
out:
	kfree(profile);
	if (fw)
		release_firmware(fw);

	if (restart_hw && !inic->shutting_down &&
	    inic->boot_attempts < inic->max_fw_upload) {
		/* RaCfgRestartiNIC() asks the MII platform to issue GPIO reset. */
		inic->firmware_running = false;
		inic->address_ready = false;
		inic->boot_seen = false;
		inic->boot_tx_dumped = false;
		reinit_completion(&inic->fw_ready_done);
		ret = rt3662_restart_inic_hw(inic);
		if (ret)
			dev_warn(inic->dev, "iNIC GPIO recovery failed: %d\n", ret);
	}

	inic->boot_in_progress = false;
	/* A BOOT_NOTIFY may arrive during the reset pulse before this flag was
	 * cleared. Consume that race here; otherwise RX schedules us later. */
	if (restart_hw && inic->boot_seen && !inic->shutting_down &&
	    inic->boot_attempts < inic->max_fw_upload)
		schedule_work(&inic->boot_work);
}

void rt3662_schedule_boot(struct rt3662_inic *inic)
{
	if (!inic->boot_in_progress && !inic->shutting_down &&
	    inic->boot_attempts < inic->max_fw_upload)
		schedule_work(&inic->boot_work);
}

/* Called once from cfg80211 registration after the parent structure exists. */
void rt3662_racfg_init_work(struct rt3662_inic *inic)
{
	INIT_WORK(&inic->boot_work, rt3662_boot_worker);
	INIT_DELAYED_WORK(&inic->heartbeat_work, rt3662_heartbeat_worker);
}
