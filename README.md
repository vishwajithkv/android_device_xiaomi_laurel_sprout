<!-- SPDX-License-Identifier: Apache-2.0 -->
# Xiaomi Mi A3: LineageOS 23.2 with Google ACK 6.18

Wi-Fi validation (2026-10-09): correcting the WLAN DT resource removes the
CE MMIO stall. Live ADB confirms wlan0 registration and, after refreshing
wificond's initial failed nl80211 discovery, a WPA3-SAE connection on 5 GHz,
IPv4 assignment and transmitted packets. The helper now publishes nl80211
readiness after loading the full-mode kernel modules; init restarts wificond
at that point. Rebuild the complete ROM and verify connection after reboot.
Select `export LAUREL_WIFI_STAGE=full`; the QMI-only diagnostic default does
not expose usable Wi-Fi. The wificond split-dump source patch is carried in
the kernel's Documentation/android/rom-patches/wificond/ directory and is
already applied in this workspace. Preserve it for a fresh sync. 2.4 GHz
association remains unverified. The paragraphs below retain earlier results.

Wi-Fi candidate (2026-10-08): the maintainer's build #23 boots Android and
reaches FW_READY in QMI-only mode. Full mode reproduced the CE-register hang
and CPU 7 RCU stalls; the maintainer subsequently reported loss of display
updates. The default is restored to qmi-only after Android boot completion to
avoid that access. Wi-Fi connectivity remains unavailable in this profile.
The boot logger separates kernel reading, writing and state snapshots and records
errors. Rebuild the complete ROM to install this candidate. See the companion kernel's
Documentation/android/WIFI_IMPLEMENTATION_20261008.md for stage selection,
source provenance and the maintainer validation sequence. See
[the reviewer entry point](../../../kernel/mainline/sm6125-mainline-6.18/Documentation/android/WIFI_REVIEW.md)
for exact integration patches and current failure evidence.

Device: laurel_sprout; Qualcomm SM6125 / Snapdragon 665 / Trinket.
This branch is `lineage-23.2-6.18-split`, using Android 16 userspace.

## Source and status

BPF-cache validation (2026-10-07): the connected Android boot with the rebuilt
Tethering APEX loads BPF successfully in an init wait of 1.369 s, down from
13.806 s; netd.o loads in 106 ms. Composer starts at 11.003 s and native
frame-synchronized brightness succeeds at 12.688 s. The maintainer reports
recovery display and startup improvement. The experimental bootloader-logo retention has been reverted after the
maintainer reported improper behavior. Earlier native/recovery display and
BPF startup corrections remain; a splash-to-Android gap can still occur.
See kernel Documentation/android/DISPLAY_STARTUP.md for evidence and checks.

BPF startup candidate (2026-10-07): build #19 still blocks init for 13.806 s.
The local Connectivity loader now reads each APEX BPF ELF once into memory,
avoiding repeated file reads during symbol/BTF fixups. New logs separate BTF
fixups from kernel BTF loading; performance improvement awaits validation.
Preserve the companion kernel's
Documentation/android/rom-patches/connectivity/0001-netbpfload-cache-elf-startup.patch
when recreating this workspace; it is already applied locally. Rebuild the
full ROM, including the Tethering APEX, to install it. A boot-only update
cannot apply this userspace correction. See DISPLAY_STARTUP.md for timings.

Display startup investigation (2026-10-07): native DRM binds at 1.254 s,
but the initial brightness transfer fails. Android's BPF loader also blocks
init for 13.245 s before composer startup. Recovery's missing-battery retry
holds its drawing mutex for up to five seconds. A local bootable/recovery
correction moves the retry outside that lock; preserve/reapply the patch at
kernel/mainline/sm6125-mainline-6.18/Documentation/android/recovery-battery-ui-lock.patch
when recreating the sources. This change needs a maintainer rebuild and
validation. See DISPLAY_STARTUP.md in the same directory for evidence and
the comparison with Laurel's 4.14 continuous-splash/first-frame handling.

Phone UI overlay correction (2026-10-07): the running ROM resolved
config_showNavigationBar=false, an empty cutout, a 28dp status bar and zero
rounded-corner content padding. The original device overlay folders were
not registered in device.mk. Although the gestural navigation package was
enabled, Settings requires WindowManager to report a navigation bar before
exposing navigation-mode controls. device.mk now includes overlay-mainline,
which enables software navigation and carries the existing Mi A3 notch,
status-bar and rounded-corner geometry. It leaves navigation-mode selection
to the standard Android overlays and user settings. Downstream fingerprint,
sensor, light and power capability overlays are not included by this change.
Rebuild the ROM to apply framework/SystemUI resource changes; this source
correction has not been built or validated on the device. Confirm padding,
navigation-mode Settings and edge-back gestures after installation.

The native profile now requests recovery-only SimpleDRM through
msm.laurel_recovery_simpledrm=1. The built-in MSM kernel early-init hook checks
the Mi A3 compatible and the exact skip_initramfs normal-boot token. Recovery
disables GPU/GMU/GPUCC/GPU-SMMU and MDSS/DSI/PHY/DISPCC live DT nodes before
platform population, preserving the bootloader framebuffer. Android retains
the native nodes and uses Freedreno/Turnip. A single recovery-as-boot image
still serves both modes; changing only a ramdisk property would be too late.
The panel module stays packaged for normal native boot but has no host to
bind in fallback recovery. The maintainer reports that the rebuilt fallback
restores the recovery display. Android's delayed display transition remains;
this recovery policy does not fix that separate native-panel startup issue.
Touch, sideload and repeated reboot checks for this revision remain to be
confirmed with device logs.

Kernel source: `kernel/mainline/sm6125-mainline-6.18`, branch `mainline-6.18-split`,
based on Google kernel/common `android17-6.18-2026-09_r5`
(`926a323dc2667901ddcf1d5b024b82eb00aa89cc`, Linux 6.18.32).
Google history is retained; the Mi A3 community patches preserve their authors.
See the kernel's `Documentation/android/README.md` and `patch-provenance.json`.

For a fresh sync, install both XML files from `local_manifests/` into
`.repo/local_manifests/`. Keep the `zz-` filename: it replaces roomservice's
downstream device selection after roomservice.xml is parsed. The kernel XML
pins both the new 6.18 source and the existing 6.15 fallback. The device XML
selects this ROM branch.

The maintainer's 2026-10-04 build booted recovery and normal Android 16 on
slot B with `6.18.32-g47faf8ef4e7b`. Live ADB confirmed boot completion,
running Zygote and SurfaceFlinger, and the launcher as the resumed activity.
Physical GUI usability remains unverified: console contents remained visible,
and the composer reported unsupported DRM VSync waits. The persistent boot
logger exited early; diagnostics were saved directly through ADB. Native GPU,
touch and encryption remain outside this verified boot milestone.
Google's Android 17 kernel is being used as a custom board kernel on Android 16,
not as a certified GKI image.

## Split source repositories

The kernel core, devicetrees and external modules are separate sibling Git
repositories under kernel/mainline/: sm6125-mainline-6.18,
sm6125-mainline-6.18-devicetrees and sm6125-mainline-6.18-modules.
The device manifest pins all three published source repositories, including
`vishwajithkv/kernel_xiaomi_laurel_sprout-devicetrees` and
`vishwajithkv/kernel_xiaomi_laurel_sprout-modules`.
See kernel Documentation/android/SPLIT_SOURCES.md for the verified Lineage
reference, ownership, build integration and fallback. The pre-split 6.18
boot result above does not validate the refactor. No hardware was enabled.

## Native graphics: current source default

configs/graphics-profile.mk now selects native graphics for the next full ROM
build: Mesa Freedreno GLES/Turnip Vulkan, MSM allocation and the upstream AIDL
DRM composer, with the matching native kernel fragment and DTB. The fresh
Samsung panel module and GPU firmware paths are packaged for recovery as well.
The maintainer's 2026-10-06 build #15 has physical native scanout at
720 x 1560 / 60 Hz and Freedreno FD610 GLES. Settings scrolling confirms
DEVICE composition for the app and status bar without new display commit
failures. Turnip execution and GPU power/performance remain unverified.
Change its default to simpledrm to rebuild the verified software-rendering
fallback described below. Use separate output directories for the two profiles.

Read kernel/mainline/sm6125-mainline-6.18/Documentation/android/NATIVE_GRAPHICS.md
for full build commands, firmware prerequisites and acceptance. Kernel and DTS manifest pins identify the matching native-profile commits.
After a fresh sync, also apply the Mesa and DRM composer patches carried in
the kernel repository as documented in NATIVE_GRAPHICS.md.
The latest 2026-10-05 validation confirms the split baseline boots Android and
physically updates the display; it does not validate native Adreno.

## First-boot profile

Retain CPU, memory, power, thermal monitoring, UFS, USB/ADB, persistent logs and
SimpleDRM. FT3518 touch and its I2C/GPI/power dependencies are enabled for the
next build, pending device validation. Native display/GPU and other optional
hardware remain disabled; their source support is retained for later stages. Rendering uses the
existing SwiftShader/ANGLE, generic minigbm and DRM framebuffer composer setup.
The kernel text framebuffer console is disabled to prevent penguins and kernel
text competing with Android. SimpleDRM and fbdev remain available for recovery
graphics. Early screen logs disappear; ADB and persistent kernel logs remain.
The normal-boot helper retains a console-detach fallback for older kernels.
See kernel Documentation/android/TOUCH_DISPLAY.md for validation and limitations.

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

For the verified unsplit 6.18 source, switch the kernel back to mainline-6.18
and this device tree to lineage-23.2-6.18, restoring its local manifests.
Keep the matching baseline images and output directory.


The old kernel checkout `kernel/mainline/sm6125-mainline` remains unchanged.
Switch this ROM device repository back to `lineage-23.2-6.15` to select it again,
and use the original ROM output directory. Keep the matching previously built
images for device recovery. The manifests pin the published 6.18 kernel commits and select this ROM branch.

## Physical display corrections

The updated composer imports minigbm image planes without treating its metadata
FD as another plane and paces unsupported vblank waits in software. Its patch
is saved in the kernel repository at Documentation/android/rom-patches/
hardware-mainline-common/0001-drmfb-import-image-planes-and-pace-simpledrm.patch
for fresh checkouts.

The next live diagnosis verified that composer was installed, then identified
the actual framebuffer rejection: SimpleDRM does not expose Android's XBGR8888
client-target format. The kernel now advertises it for native XRGB8888 scanout
and converts red/blue channels during the shadow blit. The native bootloader
format is actually ARGB8888 (a8r8g8b8); the follow-up kernel correction now
covers this alpha-bearing variant too, setting opaque alpha when converting. This needs a new kernel/
boot build, retaining the updated composer. Physical GUI operation after this
correction still requires validation; see kernel Documentation/android/
TOUCH_DISPLAY.md for captured evidence and the correction to the earlier diagnosis.

## Latest validation (2026-10-05)

The split-source build boots Android with both CPU frequency policies active.
The maintainer confirms recovery touch, completed ROM sideload and physical
display transition after the ARGB8888 correction. Rendering remains software
based and installation timing has not been quantified. Earlier pending status
paragraphs describe the history, not the latest result. Apply the composer
patch above as well as the system/core patch when recreating this build.

## Modem bringup source stage

Source pins now include the attributed laurel-connectivity PAS modem port.
This stage has not been built or device validated. MPSS registers with manual
startup; Android radio remains disabled until matching firmware, mainline RMTFS
and a compatible radio HAL/data path are integrated. See kernel
Documentation/android/MODEM.md. The validated pre-modem pins are kernel
ff4152ad6d0f, devicetrees da7d0af and device 11e169e.

## Native display validation (2026-10-06)

The latest kernel/DTS pins include attributed upstream MDSS reset backports,
SM6125 lane-clamp wiring, panel-before-video sequencing and the prepared-PLL
restart fix used in the working build #15. The maintainer confirmed physical
output and smooth Android rendering. Live captures confirm hardware composition;
launcher mixed CLIENT/DEVICE composition is not evidence of a broken composer.
The required Mesa, DRM composer, hardware/mainline/common and system/core local
changes are preserved as exact patches in the kernel's rom-patches directory.
Apply those patches once after syncing, as documented in NATIVE_GRAPHICS.md.
Keep the matching kernel, DTB and freshly built modules together.

## Organized source layout

The devicetrees repository owns qcom/ DTS sources, bindings/ carried schemas
and include/dt-bindings/ board headers. The kernel retains compatibility
symlinks to these files plus the upstream shared bindings. The external panel
module lives under qcom/opensource/display-drivers/panel/ in the modules repo.
Lineage source dependencies and the standalone kernel helper use the new paths.
All three repositories must be synced to the matching pinned revisions.
This source-only refactor needs a maintainer rebuild; the working pre-refactor
native-display build remains the validation baseline.

## Wi-Fi candidate

WCN3990 board wiring belongs to the devicetrees repo; upstream ath10k and
its vendor modules belong to the ACK kernel, not a duplicate external driver.
Android firmware links and services belong to the ROM tree. See the companion
kernel `Documentation/android/WIFI.md` for provenance, integration and pending
2.4/5 GHz validation. Build #23 reaches FW_READY in QMI-only mode; full mode still hangs at CE
initialization. Association on either band is unvalidated.

## Complete display rollback, 2026-10-08

Following the report of a stuck Lineage boot logo, all remaining uncommitted
display experiments have now been restored to the committed baseline:
DPU teardown/MMU reordering, exported DSI frame wait and panel brightness
retry changes are removed, in addition to splash retention. The panel again
uses the committed deferred 20 ms brightness worker. Earlier historical
candidate descriptions above no longer describe the current source.
Wi-Fi, recovery UI and Connectivity BPF changes remain independent.
Archived display diffs are in out/display-revert-20261008 locally. Matching
kernel and panel modules must be rebuilt together; no runtime fix is claimed
until the maintainer validates.
