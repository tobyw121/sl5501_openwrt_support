#
# Copyright (C) 2024 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=wifi-scripts
PKG_VERSION:=1.0
PKG_RELEASE:=2
PKG_LICENSE:=GPL-2.0

PKG_MAINTAINER:=Felix Fietkau <nbd@nbd.name>

include $(INCLUDE_DIR)/package.mk

define Package/wifi-scripts
  SECTION:=utils
  CATEGORY:=Base system
  DEPENDS:=+netifd +ucode +ucode-mod-nl80211 +ucode-mod-rtnl +ucode-mod-ubus +ucode-mod-uci
  TITLE:=Wi-Fi configuration scripts
  PKGARCH:=all
endef

define Package/wifi-scripts/description
 A set of scripts that handle setup and configuration of Wi-Fi devices.
endef

define Build/Prepare
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/wifi-scripts/install
	$(INSTALL_DIR) $(1)
	$(CP) ./files/* $(1)/
	# This project tree is distributed as ZIPs and executable mode bits can be
	# lost. netifd executes mac80211.sh directly and the SL5501 ready hook calls
	# /sbin/wifi, so force the two entry points executable at package-install
	# time instead of relying on source-file metadata.
	chmod 0755 $(1)/sbin/wifi
	chmod 0755 $(1)/lib/netifd/wireless/mac80211.sh
	chmod 0755 $(1)/etc/hotplug.d/ieee80211/10-wifi-detect
endef

$(eval $(call BuildPackage,wifi-scripts))
