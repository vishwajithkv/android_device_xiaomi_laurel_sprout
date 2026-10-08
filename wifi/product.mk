# SPDX-License-Identifier: Apache-2.0
# Android's standard no-vendor-HAL STA path uses wificond + AIDL supplicant
# and nl80211. Do not advertise a qcwcn vendor HAL to upstream ath10k.
# mainline_common selects the supplicant APEX. Adding standalone wpa_supplicant
# here would install a second ISupplicant/default VINTF declaration.
# Retain the diagnostic fallback unless full mode is explicitly selected.
# Full mode now has validated live 5 GHz association; reboot checks are pending.
LAUREL_WIFI_STAGE ?= qmi-only
LAUREL_WIFI_DEFERRED ?= true
ifneq ($(words $(LAUREL_WIFI_STAGE)),1)
$(error LAUREL_WIFI_STAGE must be a single profile)
endif
ifeq ($(filter $(LAUREL_WIFI_STAGE),off mpss qmi-only full),)
$(error LAUREL_WIFI_STAGE must be off, mpss, qmi-only or full)
endif
ifeq ($(filter $(LAUREL_WIFI_DEFERRED),true false),)
$(error LAUREL_WIFI_DEFERRED must be true or false)
endif
PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.laurel.wifi.stage=$(LAUREL_WIFI_STAGE) \
    ro.vendor.laurel.wifi.deferred=$(LAUREL_WIFI_DEFERRED)
PRODUCT_PACKAGES += laurel_wifi_start iw rmtfs tqftpserv qrtr-lookup \
    firmware_ath10k_WCN3990_hw1.0_firmware-5.bin \
    LaurelWifiOverlay \
    laurel_wlan_fw_link_0 \
    laurel_wlan_fw_link_1 \
    laurel_wlan_fw_link_2 \
    laurel_wlan_fw_link_3 \
    laurel_wlan_fw_link_4 \
    laurel_wlan_fw_link_5 \
    laurel_wlan_fw_link_6 \
    laurel_wlan_fw_link_7 \
    laurel_wlan_fw_link_8 \
    laurel_wlan_fw_link_9 \
    laurel_wlan_fw_link_10 \
    laurel_wlan_fw_link_11 \
    laurel_wlan_fw_link_12 \
    laurel_wlan_fw_link_13 \
    laurel_wlan_fw_link_14 \
    laurel_wlan_fw_link_15 \
    laurel_wlan_fw_link_16 \
    laurel_wlan_fw_link_17 \
    laurel_wlan_fw_link_18 \
    laurel_wlan_fw_link_19 \
    laurel_wlan_fw_link_20 \
    laurel_wlan_fw_link_21 \
    laurel_wlan_fw_link_22 \
    laurel_wlan_fw_link_23 \
    laurel_wlan_fw_link_24 \
    laurel_wlan_fw_link_25 \
    laurel_wlan_fw_link_26 \
    laurel_wlan_fw_link_27 \
    laurel_wlan_fw_link_28 \
    laurel_wlan_fw_link_29 \
    laurel_wlan_fw_link_30 \
    laurel_wlan_fw_link_31 \
    laurel_wlan_fw_link_32 \
    laurel_wlan_fw_link_33 \
    laurel_wlan_fw_link_34 \
    laurel_wlan_fw_link_35 \
    laurel_wlan_fw_link_36 \
    laurel_wlan_fw_link_37 \
    laurel_wlan_fw_link_38 \
    laurel_wlan_fw_link_39 \
    laurel_wlan_fw_link_40 \
    laurel_wlan_fw_link_41 \
    laurel_wlan_fw_link_42 \
    laurel_wlan_fw_link_43 \
    laurel_wlan_fw_link_44 \
    laurel_wlan_fw_link_45 \
    laurel_wlan_fw_link_46 \
    laurel_wlan_fw_link_47 \
    laurel_wlan_fw_link_48 \
    laurel_wlan_fw_link_49 \
    laurel_wlan_fw_link_50 \
    laurel_wlan_fw_link_51 \
    laurel_wlan_fw_link_52 \
    laurel_wlan_fw_link_53 \
    laurel_wlan_fw_link_54 \
    laurel_wlan_fw_link_55 \
    laurel_wlan_fw_link_56 \
    laurel_wlan_fw_link_57 \
    laurel_wlan_fw_link_58 \
    laurel_wlan_fw_link_59 \
    laurel_wlan_fw_link_60 \
    laurel_wlan_fw_link_61

PRODUCT_SOONG_NAMESPACES += hardware/mainline/qcom
PRODUCT_VENDOR_PROPERTIES += wifi.interface=wlan0
PRODUCT_COPY_FILES += \
    $(DEVICE_PATH)/wifi/init.laurel.wifi.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.laurel.wifi.rc \
    $(DEVICE_PATH)/wifi/wpa_supplicant_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant_overlay.conf
