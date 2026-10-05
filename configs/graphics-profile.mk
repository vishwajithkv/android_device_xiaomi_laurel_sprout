# SPDX-License-Identifier: Apache-2.0
# Select before the common product and BoardConfig graphics options are read.
# Change to simpledrm to rebuild the verified software-rendering fallback.
LAUREL_GRAPHICS_PROFILE ?= native

ifeq ($(filter native simpledrm,$(LAUREL_GRAPHICS_PROFILE)),)
$(error Unsupported LAUREL_GRAPHICS_PROFILE: $(LAUREL_GRAPHICS_PROFILE))
endif

ifeq ($(LAUREL_GRAPHICS_PROFILE),native)
TARGET_GRAPHICS := mesa
TARGET_GRAPHICS_EGL := mesa
TARGET_GRAPHICS_VULKAN := mesa
TARGET_GRAPHICS_ALLOCATOR_HAL := minigbm-upstream
TARGET_MINIGBM_PLATFORM := msm
TARGET_MINIGBM_UPSTREAM_INSIDE_APEX := false
TARGET_GRAPHICS_COMPOSER_HAL := drm_hwcomposer
TARGET_DRM_HWCOMPOSER_VARIANT := upstream
TARGET_DRM_HWCOMPOSER_HAL_INTERFACE := aidl
TARGET_DRM_HWCOMPOSER_INSIDE_APEX := false
else
TARGET_GRAPHICS := swiftshader
TARGET_GRAPHICS_EGL := angle
TARGET_GRAPHICS_VULKAN := swiftshader
TARGET_GRAPHICS_ALLOCATOR_HAL := minigbm-upstream
TARGET_MINIGBM_PLATFORM := generic
TARGET_GRAPHICS_COMPOSER_HAL := drmfb-composer
endif
