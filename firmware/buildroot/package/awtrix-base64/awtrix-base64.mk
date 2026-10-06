################################################################################
#
# awtrix-base64
#
################################################################################

# Upstream has no 1.4.0 tag. This is the 1.4.0 release commit; src/base64.hpp
# matches the existing PlatformIO densaugeo/base64 1.4.0 package byte for byte.
AWTRIX_BASE64_VERSION = 1.4.0
AWTRIX_BASE64_COMMIT = ac168f5fa2865de855384f6a2d61444ce7d92c27
AWTRIX_BASE64_SITE = $(call github,Densaugeo,base64_arduino,$(AWTRIX_BASE64_COMMIT))
AWTRIX_BASE64_SOURCE = base64_arduino-$(AWTRIX_BASE64_COMMIT).tar.gz
AWTRIX_BASE64_LICENSE = MIT
AWTRIX_BASE64_LICENSE_FILES = LICENSE
AWTRIX_BASE64_INSTALL_STAGING = YES
AWTRIX_BASE64_INSTALL_TARGET = NO

define AWTRIX_BASE64_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 0644 $(@D)/src/base64.hpp \
		$(STAGING_DIR)/usr/share/awtrix-deps/base64/src/base64.hpp
endef

$(eval $(generic-package))
