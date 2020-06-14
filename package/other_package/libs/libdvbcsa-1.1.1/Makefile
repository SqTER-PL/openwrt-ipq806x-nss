#
# Copyright (C) 2006-2010 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#
# Make by SqTER

include $(TOPDIR)/rules.mk

PKG_NAME:=libdvbcsa
PKG_VERSION:=1.1.1
PKG_RELEASE:=1

PKG_FIXUP:=autoreconf

include $(INCLUDE_DIR)/package.mk

PKG_INSTALL:=1

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Package/libdvbcsa
	SECTION:=libs
	CATEGORY:=Libraries
	TITLE:=libdvbcsa - DVB/CSA - with encryption and decryption capabilities
	URL:=http://www.videolan.org/developers/libdvbcsa.html
endef

define Package/libdvbcsa/description
	libdvbcsa is a free implementation of the DVB Common Scrambling
	Algorithm - DVB/CSA - with encryption and decryption capabilities
endef

CONFIGURE_ARGS += \
	--enable-shared \
	--enable-static \
	--enable-neon \
  
TARGET_CFLAGS := $(filter-out -O%,$(TARGET_CFLAGS)) -O3
TARGET_CFLAGS += $(FPIC) -ffunction-sections -fdata-sections
TARGET_LDFLAGS += -Wl,--gc-sections

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include/dvbcsa
	$(INSTALL_DIR) $(1)/usr/lib
	$(INSTALL_DATA) \
		$(PKG_INSTALL_DIR)/usr/include/dvbcsa/*.h \
	$(1)/usr/include/dvbcsa/
	$(CP) \
		$(PKG_INSTALL_DIR)/usr/lib/libdvbcsa.{so*,a,la} \
	$(1)/usr/lib/

endef

define Package/libdvbcsa/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) \
		$(PKG_INSTALL_DIR)/usr/lib/libdvbcsa.{so*,a,la} \
	$(1)/usr/lib/
endef

$(eval $(call BuildPackage,libdvbcsa))

