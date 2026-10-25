// SPDX-License-Identifier: GPL-2.0-only
/*
 * cfg80211/nl80211 front-end for the Ralink RT3662 iNIC host protocol.
 *
 * The firmware ABI predates cfg80211 and exposes wireless-extension style
 * commands through RACFG.  This file deliberately keeps that ABI private:
 * OpenWrt/hostapd only sees cfg80211 and nl80211.
 */
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/if_arp.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "rt3662_inic.h"

static struct rt3662_radio *rt3662_wiphy_radio(struct wiphy *wiphy)
{
	return *(struct rt3662_radio **)wiphy_priv(wiphy);
}

static struct rt3662_radio *rt3662_ndev_radio(struct net_device *dev)
{
	return container_of(dev->ieee80211_ptr, struct rt3662_radio, wdev);
}

static void rt3662_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request)
{
	struct rt3662_radio *radio = rt3662_wiphy_radio(wiphy);

	if (!request || !request->alpha2[0] || !request->alpha2[1])
		return;
	radio->alpha2[0] = request->alpha2[0];
	radio->alpha2[1] = request->alpha2[1];
	radio->alpha2[2] = '\0';
	/* The SL5501 uploads CountryCode in iNIC_ap*.dat before BOOT_STARTUP.
	 * Replaying it while the profile-owned AP is coming up can consume the
	 * single RACFG waiter for two seconds on firmware 2.5.1.5.  Only the
	 * legacy dynamic path needs a runtime regulatory SET. */
	if (radio->inic->firmware_running && radio->ap_started &&
	    !radio->profile_managed_ap && !radio->native_profile_active)
		rt3662_iw_private_set(radio, "CountryCode=%c%c",
				      radio->alpha2[0], radio->alpha2[1]);
}

static int rt3662_net_open(struct net_device *dev)
{
	struct rt3662_radio *radio = rt3662_ndev_radio(dev);

	netif_start_queue(dev);
	if (radio->inic->firmware_running)
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);
	return 0;
}

static int rt3662_net_stop(struct net_device *dev)
{
	netif_stop_queue(dev);
	netif_carrier_off(dev);
	return 0;
}

static netdev_tx_t rt3662_net_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct rt3662_radio *radio = rt3662_ndev_radio(dev);
	int ret;

	/*
	 * In SL5501 BUILT_IN_BRIDGE mode ordinary WLAN payload is switched by the
	 * iNIC/MII datapath.  The host netdev is the cfg80211/control-port facade.
	 * In the SL5501 profile-managed AP mode the full-MAC firmware also owns
	 * WPA2-PSK/EAPOL; the legacy tunnel remains available for dynamic mode.
	 */
	if (skb->protocol == htons(ETH_P_PAE)) {
		/* The uploaded WPA2-PSK profile makes the iNIC the authenticator.
		 * hostapd remains the nl80211 lifecycle owner, but must not run a
		 * second EAPOL exchange over the firmware-native one. */
		if (radio->profile_managed_ap || radio->native_profile_active) {
			dev->stats.tx_packets++;
			dev->stats.tx_bytes += skb->len;
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}
		ret = rt3662_tunnel_eapol(radio, skb->data, skb->len);
		if (ret)
			dev->stats.tx_errors++;
		else {
			dev->stats.tx_packets++;
			dev->stats.tx_bytes += skb->len;
		}
	} else {
		/* Normal data bypasses this synthetic netdev in built-in bridge mode. */
		dev->stats.tx_dropped++;
	}

	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops rt3662_netdev_ops = {
	.ndo_open = rt3662_net_open,
	.ndo_stop = rt3662_net_stop,
	.ndo_start_xmit = rt3662_net_xmit,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
};

static int rt3662_set_auth(struct rt3662_radio *radio, u16 index, u32 value)
{
	struct iwreq wrq = {};

	wrq.u.param.flags = index;
	wrq.u.param.value = value;
	return rt3662_iw_handler(radio, SIOCSIWAUTH, RT3662_IW_PARAM,
				 &wrq, NULL, 0);
}

static int rt3662_set_freq(struct rt3662_radio *radio, unsigned int channel)
{
	struct iwreq wrq = {};

	/* Old Ralink WEXT accepts channel number in m with exponent 0. */
	wrq.u.freq.m = channel;
	wrq.u.freq.e = 0;
	wrq.u.freq.i = channel;
	return rt3662_iw_handler(radio, SIOCSIWFREQ, RT3662_IW_FREQ,
				 &wrq, NULL, 0);
}

static int rt3662_set_essid(struct rt3662_radio *radio, const u8 *ssid, size_t len)
{
	struct iwreq wrq = {};
	char buf[IEEE80211_MAX_SSID_LEN + 1] = {};

	if (!ssid || !len || len > IEEE80211_MAX_SSID_LEN)
		return -EINVAL;
	memcpy(buf, ssid, len);
	wrq.u.essid.flags = 1;
	wrq.u.essid.length = len;
	return rt3662_iw_handler(radio, SIOCSIWESSID, RT3662_IW_POINT,
				 &wrq, buf, len);
}

static const char *rt3662_cipher_name(u32 cipher)
{
	switch (cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		return "TKIP";
	case WLAN_CIPHER_SUITE_CCMP:
		return "AES";
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		return "WEP";
	default:
		return "NONE";
	}
}

static u32 rt3662_iw_cipher(u32 cipher)
{
	switch (cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		return IW_AUTH_CIPHER_WEP104;
	case WLAN_CIPHER_SUITE_TKIP:
		return IW_AUTH_CIPHER_TKIP;
	case WLAN_CIPHER_SUITE_CCMP:
		return IW_AUTH_CIPHER_CCMP;
	default:
		return IW_AUTH_CIPHER_NONE;
	}
}

static int rt3662_set_genie(struct rt3662_radio *radio,
			     const struct cfg80211_beacon_data *beacon)
{
	const u8 *pos, *end;
	u8 ies[256];
	size_t n = 0;
	struct iwreq wrq = {};

	if (!beacon || !beacon->tail || !beacon->tail_len)
		return 0;

	pos = beacon->tail;
	end = pos + beacon->tail_len;
	while (pos + 2 <= end) {
		size_t len = pos[1] + 2;
		bool keep = false;

		if (pos + len > end)
			break;
		if (pos[0] == WLAN_EID_RSN)
			keep = true;
		else if (pos[0] == WLAN_EID_VENDOR_SPECIFIC && len >= 6 &&
			 pos[2] == 0x00 && pos[3] == 0x50 && pos[4] == 0xf2 && pos[5] == 0x01)
			keep = true;
		if (keep && n + len <= sizeof(ies)) {
			memcpy(ies + n, pos, len);
			n += len;
		}
		pos += len;
	}

	if (!n)
		return 0;
	wrq.u.data.length = n;
	return rt3662_iw_handler(radio, SIOCSIWGENIE, RT3662_IW_POINT,
				 &wrq, ies, n);
}

static int rt3662_config_security(struct rt3662_radio *radio,
				  const struct cfg80211_crypto_settings *crypto)
{
	u32 pairwise = crypto->n_ciphers_pairwise ? crypto->ciphers_pairwise[0] : 0;
	u32 group = crypto->cipher_group;
	u32 iw_wpa = IW_AUTH_WPA_VERSION_DISABLED;
	u32 iw_keymgmt = 0;
	const char *auth = "OPEN";
	const char *enc = "NONE";
	int ret;

	if (crypto->wpa_versions & NL80211_WPA_VERSION_2) {
		iw_wpa |= IW_AUTH_WPA_VERSION_WPA2;
		auth = "WPA2";
	} else if (crypto->wpa_versions & NL80211_WPA_VERSION_1) {
		iw_wpa |= IW_AUTH_WPA_VERSION_WPA;
		auth = "WPA";
	}

	if (pairwise)
		enc = rt3662_cipher_name(pairwise);
	else if (group)
		enc = rt3662_cipher_name(group);

	if (crypto->n_akm_suites) {
		switch (crypto->akm_suites[0]) {
		case WLAN_AKM_SUITE_PSK:
			iw_keymgmt = IW_AUTH_KEY_MGMT_PSK;
			if (crypto->wpa_versions & NL80211_WPA_VERSION_2)
				auth = "WPA2PSK";
			else if (crypto->wpa_versions & NL80211_WPA_VERSION_1)
				auth = "WPAPSK";
			break;
		case WLAN_AKM_SUITE_8021X:
			iw_keymgmt = IW_AUTH_KEY_MGMT_802_1X;
			break;
		default:
			return -EOPNOTSUPP;
		}
	}

	/* OEM RT3662 AP profiles use IEEE8021X=0 for WPA/WPA2-PSK.
	 * Only enterprise 802.1X AKM enables the firmware's IEEE8021X mode. */
	ret = rt3662_iw_private_set(radio, "IEEE8021X=%u",
				      crypto->n_akm_suites &&
				      crypto->akm_suites[0] == WLAN_AKM_SUITE_8021X);
	if (ret)
		return ret;
	ret = rt3662_iw_private_set(radio, "AuthMode=%s", auth);
	if (ret)
		return ret;
	ret = rt3662_iw_private_set(radio, "EncrypType=%s", enc);
	if (ret)
		return ret;

	ret = rt3662_set_auth(radio, IW_AUTH_WPA_VERSION, iw_wpa);
	if (ret)
		return ret;
	ret = rt3662_set_auth(radio, IW_AUTH_CIPHER_PAIRWISE,
				 pairwise ? rt3662_iw_cipher(pairwise) : IW_AUTH_CIPHER_NONE);
	if (ret)
		return ret;
	ret = rt3662_set_auth(radio, IW_AUTH_CIPHER_GROUP,
				 group ? rt3662_iw_cipher(group) : IW_AUTH_CIPHER_NONE);
	if (ret)
		return ret;
	if (iw_keymgmt) {
		ret = rt3662_set_auth(radio, IW_AUTH_KEY_MGMT, iw_keymgmt);
		if (ret)
			return ret;
	}
	return rt3662_set_auth(radio, IW_AUTH_80211_AUTH_ALG,
				IW_AUTH_ALG_OPEN_SYSTEM);
}

/*
 * Firmware 2.5.1.5 is happiest when the OEM concurrent profile is treated as
 * the AP configuration, exactly like the original host driver.  The OEM
 * get_mac_from_inic() path does not replay SSID/frequency/security after boot;
 * it calls SetRadioOn(pAd, 1) and lets the already-uploaded profile run.
 *
 * Keep a legacy dynamic path for non-default experiments, but use the native
 * profile path whenever hostapd asks for the configuration that this SL5501
 * image actually uploaded.
 */
static bool rt3662_profile_matches_ap(struct rt3662_radio *radio,
                                     const struct cfg80211_ap_settings *settings)
{
        static const char ssid_5g[] = "openwrt_5GHz";
        static const char ssid_2g[] = "openwrt_2,4GHz";
        const char *ssid = radio->is_5ghz ? ssid_5g : ssid_2g;
        unsigned int channel = radio->is_5ghz ? 40 : 1;
        const struct cfg80211_crypto_settings *crypto = &settings->crypto;
        bool pairwise_ccmp = false;
        unsigned int i;

        if (!settings->chandef.chan || settings->chandef.chan->hw_value != channel)
                return false;
        if (settings->ssid_len != strlen(ssid) ||
            memcmp(settings->ssid, ssid, settings->ssid_len))
                return false;
        if (settings->hidden_ssid != NL80211_HIDDEN_SSID_NOT_IN_USE)
                return false;
        if (!(crypto->wpa_versions & NL80211_WPA_VERSION_2))
                return false;
        if (crypto->cipher_group != WLAN_CIPHER_SUITE_CCMP)
                return false;
        for (i = 0; i < crypto->n_ciphers_pairwise; i++)
                if (crypto->ciphers_pairwise[i] == WLAN_CIPHER_SUITE_CCMP)
                        pairwise_ccmp = true;
        if (!pairwise_ccmp)
                return false;
        if (!crypto->n_akm_suites || crypto->akm_suites[0] != WLAN_AKM_SUITE_PSK)
                return false;
        return true;
}

/*
 * The SL5501 iNIC is a full-MAC AP.  Its complete AP profile is uploaded
 * before BOOT_STARTUP, so many WEXT/private SET operations are redundant at
 * runtime.  Firmware 2.5.1.5 explicitly returns -EINVAL/-EOPNOTSUPP for some
 * of those duplicate SETs (WirelessEvent=1 is the first one observed on real
 * hardware) even though the same setting from iNIC_ap*.dat is active.
 *
 * Treat only those explicit "already/profile-only/unsupported" responses as
 * profile fallback. Transport errors, timeouts and firmware-down conditions
 * remain fatal so hostapd never reports a false AP on a broken iNIC link.
 */
static int rt3662_profile_backed_ap_step(struct wiphy *wiphy, const char *step,
					int ret)
{
	if (!ret)
		return 0;
	if (ret == -EINVAL || ret == -EOPNOTSUPP) {
		wiphy_warn(wiphy,
			   "runtime AP step '%s' rejected (%d); keeping uploaded OEM profile value\n",
			   step, ret);
		return 0;
	}
	return ret;
}

static int rt3662_start_ap(struct wiphy *wiphy, struct net_device *dev,
			   struct cfg80211_ap_settings *settings)
{
	struct rt3662_radio *radio = rt3662_wiphy_radio(wiphy);
	unsigned int channel;
	int ret;

	if (!radio->inic->firmware_running &&
	    !wait_for_completion_timeout(&radio->inic->fw_ready_done,
				 msecs_to_jiffies(45000)))
		return -ETIMEDOUT;
	if (!radio->inic->firmware_running)
		return -EIO;
	if (!settings->chandef.chan)
		return -EINVAL;

	channel = settings->chandef.chan->hw_value;

	if (rt3662_profile_matches_ap(radio, settings)) {
		/* OEM concurrent boot sequence: after GET_MAC the host only turns
		 * the radio on.  SSID/channel/WPA2/AES/PSK already live in the
		 * profile uploaded by INITCFG.  Replaying SIOCSIWESSID is proven
		 * to time out on the real SL5501 and made hostapd tear both APs
		 * down again. */
		/* rt3662_cfg80211_fw_ready() already performed the OEM SetRadioOn(1)
		 * immediately after GET_MAC.  START_AP only binds hostapd/netifd to
		 * the already-running full-MAC AP. */
		radio->profile_managed_ap = true;
		radio->native_profile_active = true;
		radio->ap_started = true;
		netif_carrier_on(dev);
		wiphy_info(wiphy,
			   "AP started from uploaded OEM profile on channel %u, SSID '%.*s'\n",
			   channel, (int)settings->ssid_len, settings->ssid);
		return 0;
	}

	radio->profile_managed_ap = false;
	radio->native_profile_active = false;
	if (radio->alpha2[0] && radio->alpha2[1]) {
		ret = rt3662_iw_private_set(radio, "CountryCode=%c%c",
				      radio->alpha2[0], radio->alpha2[1]);
		if (ret)
			return ret;
	}
	ret = rt3662_profile_backed_ap_step(wiphy, "RadioOn=1",
			rt3662_iw_private_set(radio, "RadioOn=1"));
	if (ret)
		return ret;

	/* WirelessEvent=1 is already part of both uploaded boot profiles.  The
	 * production 2.5.1.5 firmware returns -EINVAL when it is repeated after
	 * startup, so do not make that harmless duplicate a hostapd blocker. */
	ret = rt3662_profile_backed_ap_step(wiphy, "WirelessEvent=1",
			rt3662_iw_private_set(radio, "WirelessEvent=1"));
	if (ret)
		return ret;

	ret = rt3662_profile_backed_ap_step(wiphy, "SIOCSIWFREQ",
			rt3662_set_freq(radio, channel));
	if (ret)
		return ret;
	ret = rt3662_profile_backed_ap_step(wiphy, "Channel",
			rt3662_iw_private_set(radio, "Channel=%u", channel));
	if (ret)
		return ret;
	ret = rt3662_profile_backed_ap_step(wiphy, "SIOCSIWESSID",
			rt3662_set_essid(radio, settings->ssid, settings->ssid_len));
	if (ret)
		return ret;
	ret = rt3662_profile_backed_ap_step(wiphy, "SSID",
			rt3662_iw_private_set(radio, "SSID=%.*s",
				      (int)settings->ssid_len, settings->ssid));
	if (ret)
		return ret;
	ret = rt3662_profile_backed_ap_step(wiphy, "HideSSID",
			rt3662_iw_private_set(radio, "HideSSID=%u",
				      settings->hidden_ssid != NL80211_HIDDEN_SSID_NOT_IN_USE));
	if (ret)
		return ret;
	ret = rt3662_profile_backed_ap_step(wiphy, "BeaconPeriod",
			rt3662_iw_private_set(radio, "BeaconPeriod=%u", settings->beacon_interval));
	if (ret)
		return ret;
	ret = rt3662_profile_backed_ap_step(wiphy, "DtimPeriod",
			rt3662_iw_private_set(radio, "DtimPeriod=%u", settings->dtim_period));
	if (ret)
		return ret;
	ret = rt3662_profile_backed_ap_step(wiphy, "WmmCapable",
			rt3662_iw_private_set(radio, "WmmCapable=1"));
	if (ret)
		return ret;

	ret = rt3662_profile_backed_ap_step(wiphy, "security",
			rt3662_config_security(radio, &settings->crypto));
	if (ret)
		return ret;
	ret = rt3662_profile_backed_ap_step(wiphy, "RSN/WPA IE",
			rt3662_set_genie(radio, &settings->beacon));
	if (ret)
		return ret;

	radio->ap_started = true;
	netif_carrier_on(dev);
	wiphy_info(wiphy, "AP started on channel %u, SSID '%.*s'\n",
		   channel, (int)settings->ssid_len, settings->ssid);
	return 0;
}

static int rt3662_stop_ap(struct wiphy *wiphy, struct net_device *dev,
			  unsigned int link_id)
{
	struct rt3662_radio *radio = rt3662_wiphy_radio(wiphy);
	int ret = 0;

	/* In the SL5501 production path phy*-ap0 is only a cfg80211/hostapd
	 * facade.  The real AP and WPA authenticator live in the iNIC profile,
	 * and ordinary payload uses the physical DSA `wifi` port.  A netifd or
	 * hostapd reconfigure must therefore not translate facade STOP_AP into
	 * RadioOn=0, otherwise a harmless userspace restart tears down the real
	 * WLAN.  The radio is still powered down on module/device removal. */
	if (radio->native_profile_active) {
		wiphy_info(wiphy,
			   "profile-native STOP_AP: preserving firmware RadioOn=1\n");
		radio->ap_started = false;
		radio->profile_managed_ap = false;
		netif_carrier_off(dev);
		return 0;
	}

	if (radio->inic->firmware_running)
		ret = rt3662_iw_private_set(radio, "RadioOn=0");
	radio->ap_started = false;
	radio->profile_managed_ap = false;
	netif_carrier_off(dev);
	return ret;
}

static int rt3662_change_beacon(struct wiphy *wiphy, struct net_device *dev,
				struct cfg80211_ap_update *info)
{
	/* Beacon construction remains in iNIC firmware; hostapd state is applied
	 * through the same profile/RACFG controls used by start_ap. */
	return 0;
}

static struct wireless_dev *rt3662_add_virtual_intf(struct wiphy *wiphy,
						     const char *name,
						     unsigned char name_assign_type,
						     enum nl80211_iftype type,
						     struct vif_params *params)
{
	struct rt3662_radio *radio = rt3662_wiphy_radio(wiphy);
	struct net_device *ndev;
	int ret;

	wiphy_info(wiphy, "add_virtual_intf request name=%s type=%u firmware=%u\n",
		   name, type, radio->inic->firmware_running);
	if (type != NL80211_IFTYPE_AP)
		return ERR_PTR(-EOPNOTSUPP);
	if (radio->ndev)
		return ERR_PTR(-EBUSY);

	ndev = alloc_netdev(0, name, name_assign_type, ether_setup);
	if (!ndev)
		return ERR_PTR(-ENOMEM);

	ndev->netdev_ops = &rt3662_netdev_ops;
	ndev->needs_free_netdev = true;
	SET_NETDEV_DEV(ndev, radio->inic->dev);
	radio->wdev.wiphy = wiphy;
	radio->wdev.netdev = ndev;
	radio->wdev.iftype = type;
	ndev->ieee80211_ptr = &radio->wdev;
	eth_hw_addr_set(ndev, radio->mac);
	if (params && is_valid_ether_addr(params->macaddr) &&
	    ether_addr_equal(params->macaddr, radio->mac))
		eth_hw_addr_set(ndev, params->macaddr);

	/* nl80211 calls add_virtual_intf with RTNL and the wiphy mutex held.
	 * Using register_netdevice() directly here re-enters the cfg80211
	 * netdev notifier, which tries to take the same wiphy mutex and
	 * deadlocks.  cfg80211_register_netdevice() marks the wdev as being
	 * registered before invoking register_netdevice(), exactly for this
	 * cfg80211 callback context. */
	ret = cfg80211_register_netdevice(ndev);
	if (ret) {
		radio->wdev.netdev = NULL;
		free_netdev(ndev);
		return ERR_PTR(ret);
	}
	radio->ndev = ndev;
	netif_carrier_off(ndev);
	wiphy_info(wiphy, "created AP netdev %s (%pM)\n", ndev->name, ndev->dev_addr);
	return &radio->wdev;
}

static int rt3662_del_virtual_intf(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct rt3662_radio *radio = rt3662_wiphy_radio(wiphy);

	if (!radio->ndev || wdev != &radio->wdev)
		return -ENODEV;
	wiphy_info(wiphy, "del_virtual_intf request name=%s ap_started=%u\n",
		   radio->ndev->name, radio->ap_started);
	if (radio->ap_started && radio->inic->firmware_running &&
	    !radio->native_profile_active)
		rt3662_iw_private_set(radio, "RadioOn=0");
	else if (radio->native_profile_active)
		wiphy_info(wiphy,
			   "profile-native del_virtual_intf: preserving firmware RadioOn=1\n");
	radio->ap_started = false;
	radio->profile_managed_ap = false;
	/* del_virtual_intf is also called with RTNL and the wiphy mutex held.
	 * Use the cfg80211-aware unregister helper so the notifier does not
	 * recursively acquire the same wiphy mutex. */
	cfg80211_unregister_netdevice(radio->ndev);
	radio->ndev = NULL;
	radio->wdev.netdev = NULL;
	wiphy_info(wiphy, "AP netdev removal completed\n");
	return 0;
}

static int rt3662_change_virtual_intf(struct wiphy *wiphy, struct net_device *dev,
				      enum nl80211_iftype type,
				      struct vif_params *params)
{
	struct rt3662_radio *radio = rt3662_wiphy_radio(wiphy);

	if (type != NL80211_IFTYPE_AP)
		return -EOPNOTSUPP;
	radio->wdev.iftype = type;
	return 0;
}

static int rt3662_add_key(struct wiphy *wiphy, struct net_device *dev,
			  int link_id, u8 key_index, bool pairwise,
			  const u8 *mac_addr, struct key_params *params)
{
	struct rt3662_radio *radio = rt3662_wiphy_radio(wiphy);
	struct iw_encode_ext *ext;
	struct iwreq wrq = {};
	size_t len = sizeof(*ext) + params->key_len;
	int ret;

	if (key_index >= 4)
		return -EINVAL;
	if (radio->profile_managed_ap || radio->native_profile_active) {
		wiphy_dbg(wiphy,
			  "profile-native AP: firmware owns WPA key installation\n");
		return 0;
	}

	ext = kzalloc(len, GFP_KERNEL);
	if (!ext)
		return -ENOMEM;

	ext->addr.sa_family = ARPHRD_ETHER;
	if (pairwise && mac_addr) {
		ether_addr_copy(ext->addr.sa_data, mac_addr);
		ext->ext_flags |= IW_ENCODE_EXT_SET_TX_KEY;
	} else {
		eth_broadcast_addr(ext->addr.sa_data);
		ext->ext_flags |= IW_ENCODE_EXT_GROUP_KEY;
	}

	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		ext->alg = IW_ENCODE_ALG_WEP;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		ext->alg = IW_ENCODE_ALG_TKIP;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		ext->alg = IW_ENCODE_ALG_CCMP;
		break;
	default:
		kfree(ext);
		return -EOPNOTSUPP;
	}

	ext->key_len = params->key_len;
	memcpy(ext->key, params->key, params->key_len);
	if (params->seq && params->seq_len) {
		memcpy(ext->rx_seq, params->seq,
		       min_t(size_t, params->seq_len, IW_ENCODE_SEQ_MAX_SIZE));
		ext->ext_flags |= IW_ENCODE_EXT_RX_SEQ_VALID;
	}
	wrq.u.encoding.flags = key_index + 1;
	wrq.u.encoding.length = len;
	ret = rt3662_iw_handler(radio, SIOCSIWENCODEEXT, RT3662_IW_POINT,
				&wrq, ext, len);
	kfree(ext);
	return ret;
}

static int rt3662_del_key(struct wiphy *wiphy, struct net_device *dev,
			  int link_id, u8 key_index, bool pairwise,
			  const u8 *mac_addr)
{
	struct rt3662_radio *radio = rt3662_wiphy_radio(wiphy);
	struct iw_encode_ext ext = {};
	struct iwreq wrq = {};

	if (key_index >= 4)
		return -EINVAL;
	if (radio->profile_managed_ap || radio->native_profile_active)
		return 0;
	ext.addr.sa_family = ARPHRD_ETHER;
	if (mac_addr)
		ether_addr_copy(ext.addr.sa_data, mac_addr);
	else
		eth_broadcast_addr(ext.addr.sa_data);
	ext.alg = IW_ENCODE_ALG_NONE;
	wrq.u.encoding.flags = (key_index + 1) | IW_ENCODE_DISABLED;
	wrq.u.encoding.length = sizeof(ext);
	return rt3662_iw_handler(radio, SIOCSIWENCODEEXT, RT3662_IW_POINT,
				 &wrq, &ext, sizeof(ext));
}

static int rt3662_set_default_key(struct wiphy *wiphy, struct net_device *dev,
				  int link_id, u8 key_index,
				  bool unicast, bool multicast)
{
	struct rt3662_radio *radio = rt3662_wiphy_radio(wiphy);

	if (key_index >= 4)
		return -EINVAL;
	if (radio->profile_managed_ap || radio->native_profile_active)
		return 0;
	return rt3662_iw_private_set(radio, "DefaultKeyID=%u", key_index + 1);
}

/*
 * hostapd's nl80211 capability probe rejects a wiphy that advertises neither
 * NL80211_CMD_AUTHENTICATE nor NL80211_CMD_CONNECT, even when the only
 * supported interface type is AP.  The RT3662 iNIC is AP-only here and its
 * firmware owns the AP SME, so station-mode connect/disconnect must never be
 * used.  Provide a deliberately unsupported pair solely so cfg80211 advertises
 * the standard CONNECT command and hostapd can proceed to START_AP.
 *
 * Keep both callbacks: cfg80211 explicitly warns when ->connect exists without
 * ->disconnect.  Since NL80211_IFTYPE_STATION is not in interface_modes, normal
 * OpenWrt operation cannot route a station interface into these stubs.
 */
static int rt3662_connect_unsupported(struct wiphy *wiphy,
				      struct net_device *dev,
				      struct cfg80211_connect_params *sme)
{
	wiphy_dbg(wiphy, "station CONNECT requested on AP-only iNIC; rejecting\n");
	return -EOPNOTSUPP;
}

static int rt3662_disconnect_unsupported(struct wiphy *wiphy,
				         struct net_device *dev, u16 reason_code)
{
	wiphy_dbg(wiphy, "station DISCONNECT requested on AP-only iNIC; rejecting\n");
	return -EOPNOTSUPP;
}

static int rt3662_change_station(struct wiphy *wiphy, struct net_device *dev,
				 const u8 *mac, struct station_parameters *params)
{
	/* The iNIC AP SME owns association state. Key installation is the point
	 * where the firmware opens protected traffic; accept hostapd's AUTHORIZED
	 * bookkeeping so nl80211 has normal FullMAC semantics. */
	return 0;
}

static int rt3662_del_station(struct wiphy *wiphy, struct net_device *dev,
			      struct station_del_parameters *params)
{
	/* Legacy RT3662 MII firmware has no host-side MLME-deauth WEXT command.
	 * It reports departures asynchronously; accepting flush/deauth requests
	 * keeps hostapd state coherent without inventing an unsupported RACFG ABI. */
	return 0;
}

static const struct cfg80211_ops rt3662_cfg80211_ops = {
	.add_virtual_intf = rt3662_add_virtual_intf,
	.del_virtual_intf = rt3662_del_virtual_intf,
	.change_virtual_intf = rt3662_change_virtual_intf,
	.add_key = rt3662_add_key,
	.del_key = rt3662_del_key,
	.set_default_key = rt3662_set_default_key,
	.start_ap = rt3662_start_ap,
	.change_beacon = rt3662_change_beacon,
	.stop_ap = rt3662_stop_ap,
	.connect = rt3662_connect_unsupported,
	.disconnect = rt3662_disconnect_unsupported,
	.change_station = rt3662_change_station,
	.del_station = rt3662_del_station,
};

static struct ieee80211_rate rt3662_rates_2g[] = {
	{ .bitrate = 10, .hw_value = 0 }, { .bitrate = 20, .hw_value = 1 },
	{ .bitrate = 55, .hw_value = 2 }, { .bitrate = 110, .hw_value = 3 },
	{ .bitrate = 60, .hw_value = 4 }, { .bitrate = 90, .hw_value = 5 },
	{ .bitrate = 120, .hw_value = 6 }, { .bitrate = 180, .hw_value = 7 },
	{ .bitrate = 240, .hw_value = 8 }, { .bitrate = 360, .hw_value = 9 },
	{ .bitrate = 480, .hw_value = 10 }, { .bitrate = 540, .hw_value = 11 },
};

static struct ieee80211_rate rt3662_rates_5g[] = {
	{ .bitrate = 60, .hw_value = 4 }, { .bitrate = 90, .hw_value = 5 },
	{ .bitrate = 120, .hw_value = 6 }, { .bitrate = 180, .hw_value = 7 },
	{ .bitrate = 240, .hw_value = 8 }, { .bitrate = 360, .hw_value = 9 },
	{ .bitrate = 480, .hw_value = 10 }, { .bitrate = 540, .hw_value = 11 },
};

#define CH2G(_n) { .band = NL80211_BAND_2GHZ, .center_freq = 2407 + 5 * (_n), .hw_value = (_n), .max_power = 20 }
#define CH5G(_n) { .band = NL80211_BAND_5GHZ, .center_freq = 5000 + 5 * (_n), .hw_value = (_n), .max_power = 20 }
static struct ieee80211_channel rt3662_channels_2g[] = {
	CH2G(1), CH2G(2), CH2G(3), CH2G(4), CH2G(5), CH2G(6), CH2G(7),
	CH2G(8), CH2G(9), CH2G(10), CH2G(11), CH2G(12), CH2G(13),
};
static struct ieee80211_channel rt3662_channels_5g[] = {
	CH5G(36), CH5G(40), CH5G(44), CH5G(48),
};

static struct ieee80211_supported_band rt3662_band_2g = {
	.channels = rt3662_channels_2g,
	.n_channels = ARRAY_SIZE(rt3662_channels_2g),
	.bitrates = rt3662_rates_2g,
	.n_bitrates = ARRAY_SIZE(rt3662_rates_2g),
	.ht_cap = {
		.ht_supported = true,
		.cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		       IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40,
		.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K,
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_4,
		.mcs = { .rx_mask = { 0xff, 0xff } },
	},
};

static struct ieee80211_supported_band rt3662_band_5g = {
	.channels = rt3662_channels_5g,
	.n_channels = ARRAY_SIZE(rt3662_channels_5g),
	.bitrates = rt3662_rates_5g,
	.n_bitrates = ARRAY_SIZE(rt3662_rates_5g),
	.ht_cap = {
		.ht_supported = true,
		.cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		       IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40,
		.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K,
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_4,
		.mcs = { .rx_mask = { 0xff, 0xff } },
	},
};

static const u32 rt3662_ciphers[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
};

static int rt3662_register_radio(struct rt3662_inic *inic, unsigned int index)
{
	struct rt3662_radio *radio = &inic->radio[index];
	struct wiphy *wiphy;
	int ret;

	wiphy = wiphy_new(&rt3662_cfg80211_ops, sizeof(struct rt3662_radio *));
	if (!wiphy)
		return -ENOMEM;

	radio->inic = inic;
	radio->index = index;
	/* OEM profile/log ordering: ra00 is 5 GHz, concurrent ra01 is 2.4 GHz. */
	radio->is_5ghz = index == 0;
	radio->alpha2[0] = '0';
	radio->alpha2[1] = '0';
	radio->alpha2[2] = '\0';
	radio->wiphy = wiphy;
	*(struct rt3662_radio **)wiphy_priv(wiphy) = radio;
	set_wiphy_dev(wiphy, inic->dev);
	wiphy->interface_modes = BIT(NL80211_IFTYPE_AP);
	wiphy->flags |= WIPHY_FLAG_HAVE_AP_SME;
	wiphy->reg_notifier = rt3662_reg_notifier;
	wiphy->cipher_suites = rt3662_ciphers;
	wiphy->n_cipher_suites = ARRAY_SIZE(rt3662_ciphers);
	wiphy->max_scan_ssids = 0;
	wiphy->max_num_csa_counters = 0;
	if (radio->is_5ghz)
		wiphy->bands[NL80211_BAND_5GHZ] = &rt3662_band_5g;
	else
		wiphy->bands[NL80211_BAND_2GHZ] = &rt3662_band_2g;

	if (!is_valid_ether_addr(radio->mac)) {
		ether_addr_copy(radio->mac, inic->mii_mac);
		radio->mac[0] |= 0x02;
		radio->mac[5] += index * 2;
	}
	ether_addr_copy(wiphy->perm_addr, radio->mac);

	ret = wiphy_register(wiphy);
	if (ret) {
		wiphy_free(wiphy);
		radio->wiphy = NULL;
		return ret;
	}
	wiphy_info(wiphy, "registered SL5501 RT3662 iNIC radio%u (%s)\n",
		   index, radio->is_5ghz ? "5 GHz" : "2.4 GHz");
	return 0;
}

int rt3662_cfg80211_register(struct rt3662_inic *inic)
{
	unsigned int i;
	int ret;

	for (i = 0; i < inic->num_radios; i++) {
		ret = rt3662_register_radio(inic, i);
		if (ret)
			goto unwind;
	}
	return 0;

unwind:
	while (i--) {
		struct rt3662_radio *radio = &inic->radio[i];

		if (radio->wiphy) {
			wiphy_unregister(radio->wiphy);
			wiphy_free(radio->wiphy);
			radio->wiphy = NULL;
		}
	}
	return ret;
}

void rt3662_cfg80211_unregister(struct rt3662_inic *inic)
{
	unsigned int i;

	for (i = 0; i < inic->num_radios; i++) {
		struct rt3662_radio *radio = &inic->radio[i];

		kfree_skb(radio->eapol_rx);
		radio->eapol_rx = NULL;
		if (radio->ndev) {
			unregister_netdev(radio->ndev);
			radio->ndev = NULL;
			radio->wdev.netdev = NULL;
		}
		if (radio->wiphy) {
			wiphy_unregister(radio->wiphy);
			wiphy_free(radio->wiphy);
			radio->wiphy = NULL;
		}
	}
}

void rt3662_cfg80211_fw_ready(struct rt3662_inic *inic)
{
	unsigned int i;

	for (i = 0; i < inic->num_radios; i++) {
		struct rt3662_radio *radio = &inic->radio[i];
		int ret;

		if (!is_valid_ether_addr(radio->mac))
			continue;
		if (radio->wiphy)
			ether_addr_copy(radio->wiphy->perm_addr, radio->mac);
		if (radio->ndev) {
			eth_hw_addr_set(radio->ndev, radio->mac);
			if (netif_running(radio->ndev))
				netif_carrier_on(radio->ndev);
		}

		/* Exact OEM concurrent-iNIC behaviour: get_mac_from_inic() calls
		 * SetRadioOn(pAd, 1) immediately after GET_MAC.  The complete AP
		 * profile was already uploaded through INITCFG, so this is the only
		 * runtime operation required to make the firmware beacon. */
		ret = rt3662_iw_private_set(radio, "RadioOn=1");
		if (ret) {
			radio->native_profile_active = false;
			dev_warn(inic->dev,
				 "radio%u OEM post-GET_MAC RadioOn=1 failed: %d\n",
				 radio->index, ret);
		} else {
			radio->native_profile_active = true;
			dev_info(inic->dev,
				 "radio%u OEM post-GET_MAC RadioOn=1 complete; profile beacon enabled\n",
				 radio->index);
		}
	}
	rt3662_heartbeat_fw_ready(inic);
}

void rt3662_cfg80211_sta_event(struct rt3662_radio *radio, const u8 *mac, bool assoc)
{
	struct station_info sinfo = {};

	if (!radio->ndev || !is_valid_ether_addr(mac))
		return;
	/* WPA2-PSK authentication/association is completed inside the iNIC when
	 * the uploaded profile owns the AP.  Feeding the same station into
	 * hostapd would start a second 4-way handshake on top of the firmware's. */
	if (radio->profile_managed_ap || radio->native_profile_active) {
		/* The iNIC firmware owns MLME/WPA in profile-managed mode.  Keep
		 * hostapd out of that state machine, but make every firmware station
		 * transition visible so periodic ageout/deauth bugs can be diagnosed
		 * from a normal sl5501-wifi-diag capture. */
		dev_info(radio->inic->dev,
			 "radio%u firmware-native station %pM %s\n",
			 radio->index, mac, assoc ? "associated" : "disassociated");
		return;
	}
	if (assoc)
		cfg80211_new_sta(radio->ndev, mac, &sinfo, GFP_ATOMIC);
	else
		cfg80211_del_sta(radio->ndev, mac, GFP_ATOMIC);
}
