include $(sort $(wildcard $(BR2_EXTERNAL_AWTRIX_PATH)/package/*/*.mk))

# Linux 6.18 refreshed the FSF address in LICENSES/preferred/GPL-2.0.
# Buildroot's built-in hash describes its older default header release; combining
# both hashes would reject the correct 6.18 license during legal-info. Use the
# complete version-specific archive and license hashes for this custom pin only.
ifeq ($(BR2_KERNEL_HEADERS_VERSION),y)
ifeq ($(call qstrip,$(BR2_DEFAULT_KERNEL_VERSION)),6.18.52)
LINUX_HEADERS_HASH_FILES = $(BR2_EXTERNAL_AWTRIX_PATH)/patches/linux-headers/6.18.52/linux-headers.hash
endif
endif
