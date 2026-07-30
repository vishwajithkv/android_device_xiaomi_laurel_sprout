#
# Copyright (C) 2021 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Inherit some common Lineage stuff.
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

# Inherit from laurel_sprout device
$(call inherit-product, device/xiaomi/laurel_sprout/device.mk)

# Mist OS
MISTOS_MAINTAINER := "leoxvis"


PRODUCT_NAME := lineage_laurel_sprout
PRODUCT_DEVICE := laurel_sprout
PRODUCT_MANUFACTURER := Xiaomi
PRODUCT_BRAND := Xiaomi
PRODUCT_MODEL := Mi A3

PRODUCT_GMS_CLIENTID_BASE := android-xiaomi

# Device specifications
PRODUCT_PRODUCT_PROPERTIES += \
    ro.mist.display=720 x 1560, 60 hz \
    ro.mist.battery=4030mah \
    ro.mist.soc=Snapdragon® 665 \
    ro.mist.camera=48MP + 8MP + 2MP \
    ro.mist.front=32MP \
    ro.mist.platform=SM6125 \
    ro.mist.screen=6.09 Super AMOLED\
    ro.mist.device.name=Mi A3

BUILD_FINGERPRINT := Xiaomi/laurel_sprout/laurel_sprout:11/RKQ1.200903.002/V12.0.26.0.RFQMIXM:user/release-keys
