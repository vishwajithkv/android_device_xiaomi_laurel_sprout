<!-- SPDX-License-Identifier: Apache-2.0 -->
# Xiaomi Mi A3: LineageOS 23.2 with Google ACK 6.18

Device: laurel_sprout; Qualcomm SM6125 / Snapdragon 665 / Trinket.
This branch is `lineage-23.2-6.18`, using Android 16 userspace.

## Source and status

Kernel source: `kernel/mainline/sm6125-mainline-6.18`, branch `mainline-6.18`,
based on Google kernel/common `android17-6.18-2026-09_r5`
(`926a323dc2667901ddcf1d5b024b82eb00aa89cc`, Linux 6.18.32).
Google history is retained; the Mi A3 community patches preserve their authors.
See the kernel's `Documentation/android/README.md` and `patch-provenance.json`.

For a fresh sync, install both XML files from `local_manifests/` into
`.repo/local_manifests/`. Keep the `zz-` filename: it replaces roomservice's
downstream device selection after roomservice.xml is parsed. The kernel XML
pins both the new 6.18 source and the existing 6.15 fallback. The device XML
selects this ROM branch. Publish the local commits before syncing elsewhere.

This migration has not been compiled or boot-tested. The previous 6.15 setup
reached Android boot completion and Settings through scrcpy. That is the first
6.18 acceptance target; source preparation is not a verified support claim.
Google's Android 17 kernel is being used as a custom board kernel on Android 16,
not as a certified GKI image.

## First-boot profile

Retain CPU, memory, power, thermal monitoring, UFS, USB/ADB, persistent logs and
SimpleDRM. Native display/GPU, touch and other optional hardware remain disabled;
their existing source support is retained for later stages. Rendering uses the
existing SwiftShader/ANGLE, generic minigbm and DRM framebuffer composer setup.
The normal-boot init helper detaches the framebuffer console before Android
starts graphics services; recovery retains its console.

Keep existing fstab, explicit `avb=vbmeta`, cgroup fixes, DMA-heap permissions,
shader-cache workaround, software security HALs and unencrypted userdata.
This branch does not restore encryption or add proprietary downstream HALs.

## Boot layout

Boot header v0, 4096-byte pages, `Image.gz` with the bringup DTB appended,
and recovery in the boot ramdisk. Boot is 64 MiB, system 3 GiB, vendor 1 GiB.
Product/system_ext remain inside system; no new partitions are introduced.
UFS boot-device path: `soc@0/4804000.ufshc`. All freshly built modules and depmod
metadata are installed in vendor; boot-critical drivers are built in.
Do not mix a 6.15 kernel, DTB or module set with the 6.18 build.

The established mainline boot prerequisite is an erased DTBO on the test slot.
Do not restore the downstream DTBO with this kernel. Preserve the working slot
and original boot/DTBO backups. There is no automatic erase, format or flash step.

## Required Android init patch

The bootloader supplies `skip_initramfs` for normal Android boot. Keep the
kernel repository's patch at
`Documentation/android/rom-patches/system-core/0001-init-recognize-legacy-normal-boot.patch`.
It recognizes the exact flag and selects the first-stage ramdisk while preserving
recovery selection. The patch is already applied in this workspace.
For a fresh checkout, first check whether it applies, then apply it once:

```sh
git -C system/core apply --check ../../kernel/mainline/sm6125-mainline-6.18/Documentation/android/rom-patches/system-core/0001-init-recognize-legacy-normal-boot.patch
git -C system/core apply ../../kernel/mainline/sm6125-mainline-6.18/Documentation/android/rom-patches/system-core/0001-init-recognize-legacy-normal-boot.patch
```

## Build and acceptance

Use the kernel documentation for standalone commands and artifact verification.
For a full ROM, use a separate output directory from the 6.15 build:

```sh
cd /home/vishwajithkv/android/lineage
export OUT_DIR=out-6.18
source build/envsetup.sh
breakfast laurel_sprout
m -j8 bacon
```

Outputs are under `out-6.18/target/product/laurel_sprout/`.
Validate boot header, appended DTB, image size and newly built modules before
installing. Test recovery first, then normal Android; collect metadata boot logs
and check `sys.boot_completed=1` and Settings through scrcpy. Existing userdata
must not be formatted automatically to conceal a boot failure.

## Fallback

The old kernel checkout `kernel/mainline/sm6125-mainline` remains unchanged.
Switch this ROM device repository back to `lineage-23.2-6.15` to select it again,
and use the original ROM output directory. Keep the matching previously built
images for device recovery. Published manifests require the referenced 6.18
kernel commits to be pushed; this migration is initially committed locally.
