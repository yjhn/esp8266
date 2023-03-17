include $(TOPDIR)/rules.mk

PKG_NAME:=devctl
PKG_VERSION:=0.1
PKG_RELEASE:=1
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/devctl
	DEPENDS:=+libubus +libubox +libblobmsg-json +libuci +libserialport +libjson-c
	CATEGORY:=Utilities
	TITLE:=Control ESP8266 devices connected to the router via USB
endef

define Package/devctl/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/etc/init.d $(1)/etc/config
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/devctl $(1)/usr/bin/devctl
	$(INSTALL_DATA) ./files/devctl.config $(1)/etc/config/devctl
	$(INSTALL_BIN) ./files/devctl.init $(1)/etc/init.d/devctl
endef

$(eval $(call BuildPackage,devctl))
