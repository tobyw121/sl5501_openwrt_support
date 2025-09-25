# SPDX-License-Identifier: GPL-2.0-only
#
# MiniUI package definition
#

include $(TOPDIR)/rules.mk

PKG_NAME:=miniui
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)

PKG_LICENSE:=GPL-2.0-only

PKG_CONFIG_DEPENDS:=CONFIG_MINIUI_REPLACES_LUCI

include $(INCLUDE_DIR)/package.mk

define Package/miniui
  SECTION:=base
  CATEGORY:=Base system
  SUBMENU:=Web Interfaces
  TITLE:=Lightweight MiniUI management interface
  DEPENDS:=+uhttpd +uhttpd-mod-ubus +rpcd +rpcd-mod-uci +rpcd-mod-rpcsys +libubus +libuci
  DEFAULT:=y if MINIUI_REPLACES_LUCI
endef

define Package/miniui/description
 MiniUI provides a lightweight web management interface intended to
 replace LuCI on devices with constrained flash or RAM footprints. The
 interface relies on rpcd endpoints for configuration and firmware
 management workflows.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) \
	-o $(PKG_BUILD_DIR)/miniui-ubus $(PKG_BUILD_DIR)/miniui-ubus.c \
	$(TARGET_LDFLAGS) -lubus -lubox -lblobmsg_json
endef

define Package/miniui/install
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/miniui $(1)/etc/init.d/miniui
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./files/etc/uci-defaults/99-miniui $(1)/etc/uci-defaults/99-miniui
	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DATA) ./files/usr/share/rpcd/acl.d/miniui.json $(1)/usr/share/rpcd/acl.d/miniui.json
	$(INSTALL_DIR) $(1)/usr/libexec/miniui
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/miniui-ubus $(1)/usr/libexec/miniui/miniui-ubus
	$(INSTALL_BIN) ./files/usr/libexec/miniui/*.sh $(1)/usr/libexec/miniui/
	$(INSTALL_DIR) $(1)/www/miniui
	$(INSTALL_DATA) ./files/www/miniui/* $(1)/www/miniui/
endef

$(eval $(call BuildPackage,miniui))
