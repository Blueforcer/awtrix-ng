################################################################################
#
# awtrix-ng
#
################################################################################

# These defaults allow Buildroot to parse the external tree before a snapshot
# is selected. An actual build requires the runner's immutable Git snapshot.
AWTRIX_NG_SNAPSHOT ?= $(BR2_EXTERNAL_AWTRIX_PATH)/.snapshot-required
AWTRIX_NG_REVISION ?= unconfigured
AWTRIX_NG_VERSION = $(AWTRIX_NG_REVISION)
AWTRIX_NG_SITE = $(AWTRIX_NG_SNAPSHOT)
AWTRIX_NG_SITE_METHOD = local
AWTRIX_NG_LICENSE = PolyForm-Noncommercial-1.0.0, OFL-1.1 (font data), MIT (bundled components), Apache-2.0 (font notices), TJpgDec
AWTRIX_NG_LICENSE_FILES = LICENSE.md THIRD-PARTY-NOTICES.md \
	LICENSES/MIT-Berry.txt LICENSES/MIT-cpp-httplib.txt \
	LICENSES/MIT-Matrix-Fonts.txt LICENSES/TJpg_Decoder.txt \
	LICENSES/MIT-PubSubClient.txt LICENSES/MIT-densaugeo-base64.txt \
	LICENSES/CMUdict-SCOWL.txt
AWTRIX_NG_DEPENDENCIES = libopenssl jpeg awtrix-base64
AWTRIX_NG_SUPPORTS_IN_SOURCE_BUILD = NO
AWTRIX_NG_CONF_OPTS = \
	-DAWTRIX_DEPS=$(STAGING_DIR)/usr/share/awtrix-deps \
	-DBUILD_TESTING=OFF

# CMake installs the application and Web UI. Include notices in the rootfs,
# too: Buildroot's separate legal-info output is not installed on the target.
define AWTRIX_NG_INSTALL_NOTICES
	$(foreach notice,$(AWTRIX_NG_LICENSE_FILES), \
		$(INSTALL) -D -m 0644 $(@D)/$(notice) \
			$(TARGET_DIR)/usr/share/licenses/awtrix-ng/$(notice)$(sep))
endef
AWTRIX_NG_POST_INSTALL_TARGET_HOOKS += AWTRIX_NG_INSTALL_NOTICES

$(eval $(cmake-package))
