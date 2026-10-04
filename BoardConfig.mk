# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2021 The LineageOS Project

DEVICE_PATH := device/xiaomi/laurel_sprout
MAINLINE_COMMON_PATH := device/mainline/common
include $(MAINLINE_COMMON_PATH)/BoardConfigMainlineCommon.mk

# A/B
AB_OTA_PARTITIONS += boot system vbmeta vendor
BOARD_USES_RECOVERY_AS_BOOT := true
TARGET_NO_RECOVERY := true

# Architecture
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_ABI2 :=
TARGET_CPU_VARIANT := cortex-a73
TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv8-a
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := cortex-a73

# Bootloader
TARGET_BOOTLOADER_BOARD_NAME := laurel_sprout

# Display
TARGET_SCREEN_DENSITY := 320

# Graphics
# Override the common bringup default (all), which includes x86-only i915/xe.
# This must follow BoardConfigMainlineCommon.mk: its options overwrite product settings.
$(call soong_config_set,minigbm_upstream,platform,generic)

# Kernel
include kernel/mainline/sm6125-mainline/Documentation/android/BoardConfigBringup.mk
# Merge after the kernel fragments to support the selected boot-control UAPI.
TARGET_KERNEL_CONFIG_EXT := $(DEVICE_PATH)/configs/ufs-bsg.config \
    $(DEVICE_PATH)/configs/android-boot.config
# postmarketOS uses an appended DTB and a v0 boot image on this bootloader.
BOARD_BOOTIMG_HEADER_VERSION := 0
BOARD_KERNEL_APPEND_DTBS := $(TARGET_KERNEL_DTB)
BOARD_KERNEL_BASE := 0x00000000
BOARD_KERNEL_PAGESIZE := 4096
BOARD_MKBOOTIMG_ARGS += --header_version $(BOARD_BOOTIMG_HEADER_VERSION) \
    --kernel_offset 0x00008000 --ramdisk_offset 0x01000000 \
    --second_offset 0x00f00000 --tags_offset 0x00000100
# Header v0 carries androidboot parameters in the cmdline, not bootconfig.
BOARD_KERNEL_CMDLINE += $(filter-out binder.impl=rust,$(MAINLINE_COMMON_KERNEL_PARAMS)) \
    $(filter-out androidboot.boot_devices=%,$(MAINLINE_COMMON_ANDROIDBOOT_PARAMS)) \
    androidboot.boot_devices=soc@0/4804000.ufshc \
    androidboot.hardware=laurel_sprout androidboot.console=tty0 \
    androidboot.insecure_adb=1 \
    console=tty0 loglevel=7 buildvariant=$(TARGET_BUILD_VARIANT)
# Hold early failures on the framebuffer instead of exhausting A/B boot retries.
# Init routes fatal errors through SysRq panic; panic=0 disables automatic restart.
BOARD_KERNEL_CMDLINE := $(filter-out panic=% androidboot.init_fatal_panic=%,$(BOARD_KERNEL_CMDLINE)) \
    androidboot.init_fatal_panic=true panic=0
TARGET_KERNEL_CLANG_COMPILE := true
TARGET_KERNEL_NO_GCC := true
TARGET_KERNEL_LLVM_BINUTILS := true

# Kernel modules
# Lineage installs every newly built module and its depmod metadata in vendor.
# Empty load lists keep optional hardware out of the first boot.
TARGET_AUTO_COLLECT_KERNEL_MODULE_DEPS := true
BOARD_VENDOR_RAMDISK_KERNEL_MODULES_LOAD :=

# Partitions
BOARD_BOOTIMAGE_PARTITION_SIZE := 67108864
BOARD_FLASH_BLOCK_SIZE := 262144
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 3221225472
BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_VENDORIMAGE_PARTITION_SIZE := 1073741824
BOARD_USES_METADATA_PARTITION := true
TARGET_COPY_OUT_PRODUCT := system/product
TARGET_COPY_OUT_SYSTEM_EXT := system/system_ext
TARGET_COPY_OUT_VENDOR := vendor
TARGET_USERIMAGES_USE_F2FS := true

# Platform
# SM6125 / Snapdragon 665; Trinket is the existing Android platform name.
TARGET_BOARD_PLATFORM := trinket

# Recovery
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/configs/fstab.laurel_sprout
TARGET_RECOVERY_PIXEL_FORMAT := RGBX_8888

# RIL
ENABLE_VENDOR_RIL_SERVICE := false

# VINTF
# Retain the existing FCM level: the bringup composer uses HIDL 2.1.
DEVICE_MANIFEST_FILE := $(DEVICE_PATH)/configs/manifest.xml

# Verified Boot
BOARD_AVB_ENABLE := true
BOARD_AVB_MAKE_VBMETA_IMAGE_ARGS += --flags 3
