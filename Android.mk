# SPDX-License-Identifier: Apache-2.0
LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE),laurel_sprout)
# External DTS/module edits must rerun Kbuild and repackage the appended DTB
# even when Image.gz itself has not changed. No recipes override Lineage's rules.
laurel_split_source_deps := $(wildcard \
    $(MI_A3_KERNEL_DEVICETREES)/qcom/*.dts \
    $(MI_A3_KERNEL_DEVICETREES)/qcom/*.dtsi \
    $(MI_A3_KERNEL_DEVICETREES)/bindings/*/*.yaml \
    $(MI_A3_KERNEL_DEVICETREES)/bindings/*/*/*.yaml \
    $(MI_A3_KERNEL_DEVICETREES)/include/dt-bindings/*/*.h \
    $(MI_A3_KERNEL_DEVICETREES)/BoardConfigDevicetrees.mk \
    $(TARGET_KERNEL_EXT_MODULE_ROOT)/qcom/opensource/display-drivers/panel/*.c \
    $(TARGET_KERNEL_EXT_MODULE_ROOT)/qcom/opensource/display-drivers/panel/Kbuild \
    $(TARGET_KERNEL_EXT_MODULE_ROOT)/BoardConfigModules.mk)

$(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/arch/arm64/boot/$(BOARD_KERNEL_IMAGE_NAME): $(laurel_split_source_deps)
$(PRODUCT_OUT)/kernel: $(laurel_split_source_deps)
$(PRODUCT_OUT)/vendor.img: $(laurel_split_source_deps)
endif
