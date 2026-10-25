include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=rt3662-inic
PKG_RELEASE:=23
PKG_LICENSE:=GPL-2.0-only
PKG_MAINTAINER:=OpenWrt SL5501 cfg80211 port
PKG_BUILD_DIR:=$(KERNEL_BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk

define KernelPackage/rt3662-inic
  SUBMENU:=Wireless Drivers
  TITLE:=Ralink RT3662/RT5392 iNIC MII cfg80211 driver (SL5501)
  DEPENDS:=@TARGET_lantiq_xrx200 +kmod-cfg80211 +wifi-scripts +iwinfo
  FILES:=$(PKG_BUILD_DIR)/rt3662_inic.ko
  AUTOLOAD:=$(call AutoProbe,rt3662_inic)
  KCONFIG:=CONFIG_WIRELESS=y
endef

define KernelPackage/rt3662-inic/description
 Native cfg80211/nl80211 host driver for the Ralink RT3662 iNIC/RT5392
 WLAN subsystem used by the Sphairon/Zyxel Speedlink 5501. The radio firmware
 is bootstrapped over the GSWIP/MII RACFG protocol; no WEXT interface is exposed
 to OpenWrt userspace.
endef

define KernelPackage/rt3662-inic/conffiles
/etc/config/wireless
endef

define Build/Prepare
	$(RM) -r $(PKG_BUILD_DIR)
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/. $(PKG_BUILD_DIR)/
endef

# OpenWrt 24.10 uses cfg80211 from the mac80211 backport tree. Compile against
# that exact API so the module uses the same cfg80211 ABI as OpenWrt userspace.
RT3662_KBUILD_FLAGS:= \
	NOSTDINC_FLAGS="$(KERNEL_NOSTDINC_FLAGS) \
	-I$(PKG_BUILD_DIR) \
	-I$(STAGING_DIR)/usr/include/mac80211-backport \
	-I$(STAGING_DIR)/usr/include/mac80211-backport/uapi \
	-I$(STAGING_DIR)/usr/include/mac80211 \
	-I$(STAGING_DIR)/usr/include/mac80211/uapi \
	-include backport/backport.h"

define Build/Compile
	+$(KERNEL_MAKE) M="$(PKG_BUILD_DIR)" \
		$(RT3662_KBUILD_FLAGS) \
		KCFLAGS="-I$(LINUX_DIR) -Wno-error" \
		modules
endef

define KernelPackage/rt3662-inic/install
	$(INSTALL_DIR) $(1)/lib/firmware/ralink/rt3662
	$(INSTALL_DATA) ./files/lib/firmware/ralink/rt3662/iNIC_ap.bin \
		$(1)/lib/firmware/ralink/rt3662/iNIC_ap.bin
	$(INSTALL_DATA) ./files/lib/firmware/ralink/rt3662/iNIC_ap.dat \
		$(1)/lib/firmware/ralink/rt3662/iNIC_ap.dat
	$(INSTALL_DATA) ./files/lib/firmware/ralink/rt3662/iNIC_ap1.dat \
		$(1)/lib/firmware/ralink/rt3662/iNIC_ap1.dat
	$(INSTALL_DATA) ./files/lib/firmware/ralink/rt3662/iNIC_e2p.bin \
		$(1)/lib/firmware/ralink/rt3662/iNIC_e2p.bin
	$(INSTALL_DATA) ./files/lib/firmware/ralink/rt3662/iNIC_e2p1.bin \
		$(1)/lib/firmware/ralink/rt3662/iNIC_e2p1.bin
	$(INSTALL_DATA) ./files/lib/firmware/ralink/rt3662/iNIC_ap_card.dat \
		$(1)/lib/firmware/ralink/rt3662/iNIC_ap_card.dat
	$(INSTALL_DATA) ./files/lib/firmware/ralink/rt3662/RT3662-version \
		$(1)/lib/firmware/ralink/rt3662/RT3662-version
	$(INSTALL_DATA) ./files/lib/firmware/ralink/rt3662/RT5392-version \
		$(1)/lib/firmware/ralink/rt3662/RT5392-version
	ln -sf iNIC_ap.bin $(1)/lib/firmware/ralink/rt3662/RT3662-iNIC_ap.bin
	ln -sf iNIC_ap.bin $(1)/lib/firmware/ralink/rt3662/RT5392-iNIC_ap.bin
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) ./files/usr/sbin/sl5501-wifi-diag $(1)/usr/sbin/sl5501-wifi-diag
	$(INSTALL_BIN) ./files/usr/sbin/sl5501-wifi-autostart $(1)/usr/sbin/sl5501-wifi-autostart
	$(INSTALL_DIR) $(1)/etc/config $(1)/etc/hotplug.d/ieee80211 $(1)/etc/init.d $(1)/etc/uci-defaults
	$(INSTALL_CONF) ./files/etc/config/wireless $(1)/etc/config/wireless
	$(INSTALL_BIN) ./files/etc/hotplug.d/ieee80211/90-sl5501-wifi-autostart \
		$(1)/etc/hotplug.d/ieee80211/90-sl5501-wifi-autostart
	$(INSTALL_BIN) ./files/etc/init.d/sl5501-wifi $(1)/etc/init.d/sl5501-wifi
	$(INSTALL_BIN) ./files/etc/uci-defaults/99-sl5501-router-v19 \
		$(1)/etc/uci-defaults/99-sl5501-router-v19
endef

$(eval $(call KernelPackage,rt3662-inic))
