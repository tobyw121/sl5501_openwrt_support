/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _RT3662_INIC_H
#define _RT3662_INIC_H

#include <linux/completion.h>
#include <linux/etherdevice.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/if_ether.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/wireless.h>
#include <net/cfg80211.h>

#define RT3662_DRV_NAME "rt3662-inic"
#define RT3662_MAX_RADIOS 2
#define RT3662_MAX_FEEDBACK 1024
#define RT3662_BOOT_RETRIES 10 /* OEM retry counter after the initial TX */
#define RT3662_BOOT_TX_ATTEMPTS (RT3662_BOOT_RETRIES + 1)
#define RT3662_BOOT_TIMEOUT_MS 1000
#define RT3662_MAX_FW_UPLOADS 5
#define RT3662_GET_MAC_TIMEOUT_MS 40000
#define RT3662_CRC_HEADER_LEN 40
#define RT3662_MAX_EXT_EEPROM 1024
#define RT3662_MAGIC 0x18142880
#define RT3662_ETH_P_RACFG 0xffff

#define RT3662_CMD_TYPE_RSP_FLAG       0x8000
#define RT3662_CMD_TYPE_PASSIVE_MASK   0x7fff
#define RT3662_CMD_TYPE_BOOTSTRAP      0x0001
#define RT3662_CMD_TYPE_ASYNC          0x0005
#define RT3662_CMD_TYPE_COPY_TO_USER   0x0006
#define RT3662_CMD_TYPE_IWREQ_STRUC    0x0007
#define RT3662_CMD_TYPE_IOCTL_STATUS   0x0008
#define RT3662_CMD_TYPE_SYNC           0x0009
#define RT3662_CMD_TYPE_IW_HANDLER     0x000a
#define RT3662_CMD_TYPE_TUNNEL         0x000b

#define RT3662_CMD_CONSOLE             0x0000
#define RT3662_CMD_WSC_UPDATE_CFG      0x0001
#define RT3662_CMD_WIRELESS_SEND_EVENT 0x0002
#define RT3662_CMD_SEND_DAEMON_SIGNAL  0x0003
#define RT3662_CMD_HEART_BEAT          0x0004
#define RT3662_CMD_WIRELESS_SEND_EVENT2 0x0005

#define RT3662_CMD_GET_MAC             0x0000
#define RT3662_CMD_MBSS_OPEN           0x0001
#define RT3662_CMD_MBSS_CLOSE          0x0002

#define RT3662_CMD_BOOT_NOTIFY         0x0001
#define RT3662_CMD_BOOT_INITCFG        0x0002
#define RT3662_CMD_BOOT_UPLOAD         0x0003
#define RT3662_CMD_BOOT_STARTUP        0x0004
#define RT3662_CMD_BOOT_RESET          0x0007

#define RT3662_DEV_TYPE_CONCURRENT     0x0100
#define RT3662_FLAG_BIG_ENDIAN         BIT(0)

#define RT3662_RT_PRIV_IOCTL            (SIOCIWFIRSTPRIV + 0x01)
#define RT3662_RTPRIV_IOCTL_SET         (SIOCIWFIRSTPRIV + 0x02)
#define RT3662_RTPRIV_IOCTL_GET_MAC_TABLE (SIOCIWFIRSTPRIV + 0x0f)

#define RT3662_ASSOC_EVENT_FLAG         0x0101
#define RT3662_DISASSOC_EVENT_FLAG      0x0102

/* Values are part of the Ralink host/iNIC ABI. */
enum rt3662_iw_type {
	RT3662_IW_NAME = 0,
	RT3662_IW_POINT,
	RT3662_IW_PARAM,
	RT3662_IW_FREQ,
	RT3662_IW_MODE,
	RT3662_IW_QUALITY,
	RT3662_IW_SOCKADDR,
	RT3662_IW_U32,
};

struct rt3662_racfg_hdr {
	__le32 magic_no;
	__le16 command_type;
	__le16 command_id;
	__le16 length;
	__le16 sequence;
	__le16 dev_id;
	__le16 dev_type;
	__le16 command_seq;
	__le16 flags;
	__le32 status;
	__le32 reserved[2];
	u8 data[];
} __packed;

struct rt3662_inic;

struct rt3662_radio {
	struct rt3662_inic *inic;
	struct wiphy *wiphy;
	struct wireless_dev wdev;
	struct net_device *ndev;
	unsigned int index;
	bool ap_started;
	/* v20: the SL5501 production profiles own SSID/channel/WPA2/PSK. */
	bool profile_managed_ap;
	/* v22: firmware-native profile AP remains live across hostapd/netifd
	 * facade teardown/recreate cycles. */
	bool native_profile_active;
	bool is_5ghz;
	u8 mac[ETH_ALEN];
	char alpha2[3];
	u16 eapol_tx_cmdseq;
	u16 eapol_rx_cmdseq;
	u16 eapol_rx_seq;
	struct sk_buff *eapol_rx;
};

struct rt3662_sync_rsp {
	bool armed;
	u16 cmd_type;
	u16 cmd_id;
	u16 seq;
	u16 dev_type;
	u16 cmd_seq;
	int status;
	size_t len;
	u8 data[RT3662_MAX_FEEDBACK + 128];
};

struct rt3662_inic {
	struct device *dev;
	struct net_device *master;
	/* OEM MII identity shared with U-Boot/Linux ethaddr. */
	u8 mii_mac[ETH_ALEN];
	bool master_opened;
	bool master_promisc;
	bool master_allmulti;
	bool boot_mc_added;
	bool boot_tx_dumped;
	bool master_configured;
	bool bridge;
	bool syncmiimac;
	bool csumoff;
	unsigned int max_fw_upload;
	struct gpio_desc *power_gpio;
	struct gpio_desc *reset_gpio;
	struct packet_type pt;
	struct mutex cmd_lock;
	spinlock_t sync_lock;
	struct completion sync_done;
	struct completion fw_ready_done;
	struct rt3662_sync_rsp sync;
	struct work_struct boot_work;
	/* OEM firmware emits RACFG heartbeat feedback.  Monitor it only after
	 * the first real heartbeat was observed, avoiding false recovery on
	 * firmware revisions that do not emit this optional stream. */
	struct delayed_work heartbeat_work;
	unsigned long heartbeat_monitor_started;
	unsigned long heartbeat_last_rx;
	u32 heartbeat_rx_count;
	u32 heartbeat_report_ticks;
	bool heartbeat_seen;
	bool heartbeat_warned_absent;
	bool heartbeat_recovering;
	bool shutting_down;
	bool firmware_running;
	bool address_ready;
	bool boot_seen;
	bool boot_in_progress;
	unsigned int boot_attempts;
	unsigned int num_radios;
	struct rt3662_radio radio[RT3662_MAX_RADIOS];
};

int rt3662_racfg_send(struct rt3662_inic *inic, unsigned int radio,
		      u16 type, u16 id, u16 seq, u16 cmd_seq,
		      u16 dev_type, u16 dev_id, const void *data, size_t len);
int rt3662_racfg_xfer(struct rt3662_inic *inic, unsigned int radio,
		      u16 type, u16 id, u16 seq, u16 cmd_seq,
		      u16 dev_type, u16 dev_id, const void *data, size_t len,
		      void *reply, size_t *reply_len, unsigned int timeout_ms);
int rt3662_iw_handler(struct rt3662_radio *radio, u16 cmd,
		      enum rt3662_iw_type type, struct iwreq *wrq,
		      void *extra, size_t extra_len);
int rt3662_iw_private_set(struct rt3662_radio *radio, const char *fmt, ...)
	__printf(2, 3);
int rt3662_tunnel_eapol(struct rt3662_radio *radio, const u8 *data, size_t len);
void rt3662_handle_rx(struct rt3662_inic *inic, struct sk_buff *skb);
void rt3662_schedule_boot(struct rt3662_inic *inic);
void rt3662_racfg_init_work(struct rt3662_inic *inic);
void rt3662_heartbeat_fw_ready(struct rt3662_inic *inic);
void rt3662_heartbeat_stop(struct rt3662_inic *inic);
int rt3662_apply_port5_tx_delay(struct rt3662_inic *inic);
int rt3662_restart_inic_hw(struct rt3662_inic *inic);

int rt3662_cfg80211_register(struct rt3662_inic *inic);
void rt3662_cfg80211_unregister(struct rt3662_inic *inic);
void rt3662_cfg80211_fw_ready(struct rt3662_inic *inic);
void rt3662_cfg80211_sta_event(struct rt3662_radio *radio, const u8 *mac, bool assoc);

#endif
