# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2021 The LineageOS Project

DEVICE_PATH := device/xiaomi/laurel_sprout

# A/B
AB_OTA_UPDATER := true
TARGET_USES_MAINLINE_COMMON_AB_DEFS := true
PRODUCT_PACKAGES += \
    android.hardware.boot-service.qti \
    android.hardware.boot-service.qti.recovery
PRODUCT_PACKAGES_DEBUG += bootctl
# Mainline exports standard UFS BSG UAPI, not Qualcomm 4.14 UFS query ioctls.
$(call soong_config_set_bool,QTI_GPT_UTILS,USE_BSG_FRAMEWORK,true)

# AAPT
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := xhdpi

# Boot animation
TARGET_SCREEN_HEIGHT := 1280
TARGET_SCREEN_WIDTH := 720

# Bringup options
TARGET_INITIAL_BRINGUP := true
TARGET_AUDIO_HAL := default-aidl
TARGET_CAMERA_PROVIDER_HAL :=
TARGET_BLUETOOTH_HAL :=
include $(DEVICE_PATH)/configs/graphics-profile.mk
TARGET_HAS_VIBRATOR := false
TARGET_SENSORS_HAL :=
TARGET_SUPPORTS_HARDWARE_BACKED_SECURITY := false
TARGET_SUPPORTS_SUSPEND := false
include device/mainline/common/optional/options.mk
$(call inherit-product, device/mainline/common/mainline_common.mk)

# Cgroups
# API 28 adds downstream schedtune defaults; vendor overrides load last.
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/configs/cgroups.json:$(TARGET_COPY_OUT_VENDOR)/etc/cgroups.json \
    $(DEVICE_PATH)/configs/task_profiles.json:$(TARGET_COPY_OUT_VENDOR)/etc/task_profiles.json

# Fastboot
TARGET_BOARD_FASTBOOT_INFO_FILE := $(DEVICE_PATH)/fastboot-info.txt

# Graphics
ifeq ($(LAUREL_GRAPHICS_PROFILE),native)
# Use native GLES first for framework/UI rendering; Turnip is also packaged.
PRODUCT_VENDOR_PROPERTIES += \
    ro.hardware.vulkan=freedreno \
    vendor.minigbm.debug=nocompression
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    debug.hwui.renderer=skiagl \
    debug.renderengine.backend=skiaglthreaded
# Signed ZAP is already present in the device's extracted vendor source.
# Reference it directly: do not import proprietary HALs or commit firmware blobs.
LAUREL_ZAP_SOURCE := vendor/xiaomi/laurel_sprout/proprietary/vendor/firmware/a610_zap.elf
LAUREL_SQE_SOURCE := external/linux-firmware-mainline/firmware/qcom/a630_sqe.fw
ifeq ($(wildcard $(LAUREL_ZAP_SOURCE)),)
$(error Missing Mi A3 signed GPU firmware: $(LAUREL_ZAP_SOURCE))
endif
ifeq ($(wildcard $(LAUREL_SQE_SOURCE)),)
$(error Missing upstream SQE firmware: $(LAUREL_SQE_SOURCE))
endif
# Soong owns the vendor SQE destination; keep the direct copy only for recovery.
PRODUCT_PACKAGES += linux_firmware_qcom-a630
PRODUCT_COPY_FILES += \
    $(LAUREL_ZAP_SOURCE):$(TARGET_COPY_OUT_VENDOR)/firmware/qcom/sm6125/xiaomi/laurel/a610_zap.mbn \
    $(LAUREL_ZAP_SOURCE):recovery/root/vendor/firmware/qcom/sm6125/xiaomi/laurel/a610_zap.mbn \
    $(LAUREL_SQE_SOURCE):recovery/root/vendor/firmware/qcom/a630_sqe.fw
else
PRODUCT_PACKAGES += libEGL_angle libGLESv1_CM_angle libGLESv2_angle
# System ANGLE preserves SDK variants for its APK and matches EGL Loader.cpp.
$(call soong_config_set_bool,angle,angle_in_vendor,false)
$(call soong_config_set_bool,drmfb_composer,uses_minigbm,true)
PRODUCT_VENDOR_PROPERTIES += vendor.minigbm.generic_backend=dumb_generic
endif
# Keep startup shader-cache warming disabled during native driver validation.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += service.sf.prime_shader_cache=false

# Heap
$(call inherit-product, frameworks/native/build/phone-xhdpi-4096-dalvik-heap.mk)

# Init
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/configs/fstab.laurel_sprout:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.laurel_sprout \
    $(DEVICE_PATH)/configs/fstab.laurel_sprout:recovery/root/first_stage_ramdisk/system/etc/fstab.laurel_sprout \
    $(DEVICE_PATH)/configs/fstab.laurel_sprout:recovery/root/system/etc/fstab.laurel_sprout \
    $(DEVICE_PATH)/configs/init.laurel_sprout.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.laurel_sprout.rc \
    $(DEVICE_PATH)/configs/init.recovery.laurel_sprout.rc:recovery/root/system/etc/init/init.recovery.laurel_sprout.rc \
    $(DEVICE_PATH)/configs/ueventd.laurel_sprout.rc:$(TARGET_COPY_OUT_VENDOR)/etc/ueventd.laurel_sprout.rc
PRODUCT_PACKAGES += init.laurel_sprout.mainline laurel_boot_logger

# Kernel compatibility
PRODUCT_OTA_ENFORCE_VINTF_KERNEL_REQUIREMENTS := false

# Properties
PRODUCT_VENDOR_PROPERTIES += ro.radio.noril=true
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += persist.sys.usb.config=adb

# Shipping API level
PRODUCT_SHIPPING_API_LEVEL := 28

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(DEVICE_PATH) \
    external/minigbm-upstream \
    hardware/qcom-caf/bootctrl \
    kernel/mainline/configs

# WCN3990 STA connectivity (upstream ath10k SNOC).
include $(DEVICE_PATH)/wifi/product.mk
