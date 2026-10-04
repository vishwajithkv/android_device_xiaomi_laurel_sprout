<!-- SPDX-License-Identifier: Apache-2.0 -->
# Xiaomi Mi A3 mainline Android bringup

Device: **laurel_sprout**. SoC: **SM6125 / Snapdragon 665 / Trinket**.
This is the existing LineageOS ROM device tree, converted at the maintainer's
request. Its product name remains `lineage_laurel_sprout`.

## Status

The maintainer completed a LineageOS 23.2 ROM build. Linux 6.15, recovery
userspace, USB ADB and recovery display now boot after erasing the active
slot's DTBO. Normal Android boot and userdata decryption remain unverified.
Two earlier boot-image experiments returned to fastboot with an older kernel
binary and a custom empty DTBO image.

The checked-out Android sources are **LineageOS 23.2 / Android 16**, despite
the phone currently running LineageOS 24. Mainline dependencies are pinned
to matching 23.2 revisions in `local_manifests/laurel-mainline.xml`.
Do not substitute the workspace's 24.0 HAL/device trees into this checkout.

## Kernel and modules

- Source: `kernel/mainline/sm6125-mainline`, Linux 6.15 community Mi A3 port.
- Published source: https://github.com/vishwajithkv/laurel_sprout_mainline
- Hardware baseline: SzczurekYT/linux laurel,
  `39f1dc8c31fd87fa9179b6ed4d628fe69e0cd78a`.
- Config merge: `laurel_pmos_defconfig`, `android-mainline.config`,
  `laurel_sprout.config`, then `laurel_bringup.config`.
  The ROM then merges `configs/ufs-bsg.config` for its boot-control HAL and
  `configs/android-boot.config` for ext4 SELinux label support.
- DTB: `qcom/sm6125-xiaomi-laurel-sprout-bringup.dtb`.
- The full native-display DTS and driver sources remain in the kernel.
- Lineage's inline kernel build compiles modules and installs all resulting
  `.ko` files plus depmod metadata in `/vendor/lib/modules`.
- Module load lists are empty. UFS, USB and bootloader framebuffer support
  must remain built in. No old module binary is selected by this device tree.

All **1,284 modules** from the previous maintainer build, its module metadata,
Image/Image.gz, compiled DTB and config are retained under
`kernel/mainline/artifacts/laurel_sprout/previous-build`. Its manifest and
SHA256SUMS identify that older build; they do not validate the new profile.

## First-boot scope

CPU, memory, UFS, RPM power, PMIC thermal monitoring and USB are retained.
SimpleDRM uses the bootloader framebuffer, with SwiftShader/ANGLE rendering,
minigbm-upstream's dumb backend and the DRM framebuffer composer. The composer
uses its cros_gralloc importer, whose handle layout matches this allocator.
ANGLE libraries install in `/system/lib*`, matching this branch's EGL loader
and preserving the SDK variants required by the ANGLE APK.
The allocator's compile-time platform is `generic`. BoardConfig overrides the
common bringup default after including it, so Intel i915/xe code is excluded
on ARM. The common tree may still print its "Enabling all platforms" warning
before this device override is applied.

Native Adreno/DPU/panel takeover, touch, cameras, audio hardware, modem,
Bluetooth, Wi-Fi and other optional kernel peripherals are disabled for this
stage. Existing downstream ROM files remain in Git, but their common tree,
proprietary HAL packages and device overlays are no longer inherited.
Software KeyMint/Gatekeeper and generic bringup health/power HALs are selected.

This profile uses permissive SELinux and unauthenticated ADB for early logs.
`androidboot.insecure_adb=1` activates the common libinit debugging option.
Remove these settings after the bringup stages in the mainline guide pass.

## Boot and storage

The stock bootloader signals normal boot with `skip_initramfs`, not
`androidboot.force_normal_boot=1`. The mainline kernel starts the combined
ramdisk anyway; unmodified init sees `/system/bin/recovery` and skips first-stage
mounting. The kernel repository's
`Documentation/android/rom-patches/system-core/0001-init-recognize-legacy-normal-boot.patch`
makes init recognize the exact legacy flag and switch to `first_stage_ramdisk`.
The patch is applied to this checkout's `system/core/init/first_stage_init.cpp`.
A fresh checkout must apply it from the Android root with `git -C system/core
apply ../../kernel/mainline/sm6125-mainline/Documentation/android/rom-patches/system-core/0001-init-recognize-legacy-normal-boot.patch`.
Recovery boot remains selected when the bootloader omits `skip_initramfs`.

Runtime evidence on 2026-10-04: Linux 6.15 and the recovery ramdisk booted after
erasing the target slot's DTBO. Normal boot previously entered recovery directly,
with `First stage mount skipped (recovery mode)` in dmesg. System, vendor and
metadata have ext4 signatures; userdata currently has no F2FS signature visible
in recovery. Android normal boot and access to existing data remain unverified.

The next installed build reached the kernel logo and rebooted before Android
boot animation. Its resolved config had `CONFIG_EXT4_FS_SECURITY` disabled,
which prevents ext4 SELinux label access needed by Android init. The final
`configs/android-boot.config` fragment enables it. This is a source correction;
the bootloop cause is not confirmed because recovery contained no previous-boot
pstore logs. Recovery running from ramdisk does not validate ext4 label support.
The maintainer must rebuild and inspect the resolved config before retrying.

The maintainer retried: the running recovery kernel confirms
`CONFIG_EXT4_FS_SECURITY=y`, but normal boot still loops before boot animation.
Ramoops registers successfully and `/sys/fs/pstore` remains empty. The ROM
cmdline now replaces `panic=-1` with `panic=0` and sets
`androidboot.init_fatal_panic=true`, so init fatal errors trigger a panic and
hold the kernel console on screen rather than immediately rebooting. This is
a temporary diagnostic change, not a bootloop fix. Record the final console
messages on the next normal boot; if it still restarts, report that too.
A hardware watchdog or firmware reset may still restart a halted kernel.
Restore normal panic/reboot behavior after identifying the fatal error.


QTI boot-control selects `USE_BSG_FRAMEWORK=true`, using the exported
`scsi/scsi_bsg_ufs.h` UAPI rather than downstream `scsi/ufs/ioctl.h`.
The final ROM kernel fragment enables UFS BSG and SCSI generic nodes built in.
GPT/slot updates and boot-LUN selection still require maintainer validation;
a successful build does not establish that these operations work on the phone.


The postmarketOS Mi A3 recipe uses a v0 boot header and `Image.gz` with an
appended DTB. Lineage appends the selected DTB when installing its kernel;
`BOARD_KERNEL_IMAGE_NAME` remains `Image.gz`, the actual Linux make target.
Page size is 4096. The original partition sizes remain boot 64 MiB,
system 3 GiB and vendor 1 GiB. There is no new super/vendor_boot/dlkm partition.
Product and system_ext content live inside the physical system partition.

The old downstream DTBO is incompatible with this mainline device tree.
postmarketOS documents **erasing DTBO**, rather than installing our earlier
custom no-op table. DTBO is intentionally absent from the new image/OTA lists.
The maintainer must handle DTBO on the intended test slot explicitly, keeping
the working slot and original boot/DTBO backups available. No erase or flash
was performed by this integration.

Fstab is installed in vendor and in both normal and first-stage locations
under `recovery/root/`. Recovery is packaged inside `boot.img`, with no separate
recovery partition. These copies must not go into `root/`: that baseline is
also included in the system-as-root filesystem image.
Keep the empty `out/target/product/laurel_sprout/root/system` mount point
created by Android's `system/core/rootdir/create_root_structure.mk`. Removing
it from incremental output leaves target-files metadata without the required
`system` directory entry. No fstab file belongs inside that staging directory.
`androidboot.hardware=laurel_sprout` selects these files. System, vendor and
metadata mount in first stage, followed by userdata in late-fs. Partition
names use `/dev/block/by-name`, independent of the old bootdevice symlink.
System and vendor use `avb=vbmeta` explicitly. The mainline DT does not supply
Android `vbmeta/parts`; a bare `avb` flag leaves first-stage init without an
AVB partition list and aborts with `Missing vbmeta partitions`. This matches
the maintainer's captured normal-boot error. The explicit name lets init create
the active-slot `vbmeta_a` or `vbmeta_b` node before mounting system. Both
partitions were observed in recovery. This discovery failure is distinct
from a vbmeta signature verification failure. The source fix awaits a rebuilt
boot image and maintainer validation; AVB remains enabled.

The current bringup fstab disables encryption at the maintainer's request.
`configs/fstab.laurel_sprout.encrypted` preserves the encrypted configuration
for later integration. Neither profile uses `formattable` flags. When encryption is restored, software
KeyMint may be unable to unwrap data keys created by the old Qualcomm security
HAL; preserving the fstab does **not** guarantee access to existing encrypted
data. Do not format userdata or metadata to hide this failure. Capture recovery
and vold logs first. Do not expect this kernel to work with the installed
stock vendor image; the new mainline HALs require the matching rebuilt ROM.

## Encrypted storage

After the cgroup fix, normal boot requested recovery with
`--prompt_and_wipe_data --reason=init_user0_failed`. This identifies a failure
in user-zero encrypted storage setup, not proof of damaged data. The maintainer
confirmed formatting in the old recovery before installing this ROM. Recovery
cannot see a raw F2FS signature on userdata; metadata encryption can obscure
the underlying filesystem, so this alone does not establish an unformatted
partition. No formatting was performed by this integration.

The running kernel had both `CONFIG_SCSI_UFS_CRYPTO` and
`CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK` disabled while fstab requested
inlinecrypt. The final device fragment now enables software inline encryption
fallback. This corrects a configuration gap; the exact vold failure still
needs normal-boot logs and the rebuilt kernel needs maintainer validation.
Metadata encryption also needs separate validation: this kernel tree has no
`dm-default-key` driver. Do not remove encryption flags or format again to
hide the problem.

The maintainer retried with kernel build #3 (2026-10-04 12:40:51);
recovery confirmed software inline-encryption fallback enabled, but still
reported `init_user0_failed`. At the maintainer's request to prioritize boot
without encryption, the active fstab now omits `inlinecrypt`, `fileencryption`
and `keydirectory`. Its system/vendor AVB and metadata entries are unchanged.
The encrypted configuration is retained as `fstab.laurel_sprout.encrypted`.

This change is temporary and requires rebuilt boot and vendor images. It
does not decrypt or reformat existing userdata. A partition encrypted by the
old recovery cannot become readable merely by disabling encryption; a fresh
unencrypted F2FS filesystem may be necessary, which is a separate destructive
maintainer decision. No format or partition change was performed. No claim
of successful boot is made until the maintainer reports it. Restore working
encryption before treating this build as a daily-use ROM.

## Cgroups

Normal boot reached second-stage init after the vbmeta discovery fix. The
maintainer captured `Unknown subsys name schedtune`, followed by missing
`/sys/fs/cgroup/system/uid_0` process groups and an `apexd-bootstrap` failure
that requested reboot to bootloader. API 28 compatibility profiles introduce
this downstream controller even though the mainline kernel lacks it.

Device `configs/cgroups.json` marks schedtune optional; the platform retains
its CPU, cpuset, blkio and cgroup-v2 controllers. Vendor overrides load after
API-specific defaults. `configs/task_profiles.json` restores this branch's
platform CPU-group actions for the nine legacy performance profiles.
CpuPolicySpread/Pack are temporary no-ops: mainline lacks both schedtune
prefer-idle and the downstream latency-sensitive uclamp attribute. This
bringup profile does not provide those scheduling hints. Shipping API remains
28. These changes need a rebuilt vendor image and maintainer boot validation.

## Shared memory and current boot stage

After the maintainer formatted userdata from the mainline recovery, saved
boot `4c43b910-9f9f-415e-868f-6d8784c891be` confirms `/data` mounts read-write
as F2FS and Android reaches Zygote and SurfaceFlinger. SurfaceFlinger reports
missing `/dev/ashmem`, failed FMQ mmap and NO_RESOURCES errors. The built
kernel enables MEMFD_CREATE and MEMFD_ASHMEM_SHIM, but libcutils requires
`sys.use_memfd=true` to select them. Device early-init now sets this property.
The source correction needs a rebuilt vendor image and boot validation.

The same boot shows repeated Zygote restarts and drmfb vsync returning
Operation not supported. The rotated logs do not establish the cause of all
restarts; enabling memfd does not validate the display/vsync path. Logcat
retention is expanded to keep more startup evidence on the next attempt.
No additional formatting is needed to address these observed issues.

Boot `9319b44d-a311-4176-b82c-5fdf34756da8` reaches system_server and
NetBpfLoad reports success. Its pre-watchdog ANR trace shows the system_server
display thread waiting for SurfaceFlinger's getBootDisplayModeSupport call,
while the main thread waits for the display lock. SurfaceFlinger waits for
composition work; RenderEngine is compiling ANGLE shaders inside Skia's
primeShaderCache (image dimming warm-up), using SwiftShader Vulkan.
The product now sets `service.sf.prime_shader_cache=false` to bypass that
optional startup work. This is a targeted bringup experiment, not a verified
fix: normal frame rendering still compiles shaders and requires a boot test.
The drmfb composer already has a timed software vsync fallback when DRM
vblank is unsupported, despite logging an error each time. Those messages
alone do not establish the cause of this rendering wait. The captured logs
do not contain the final reboot reason.

Later boot `176cdfbb-b9b0-4949-b96f-aa552f6cae07` reaches
`sys.boot_completed=1` at about 193 seconds, delivers BOOT_COMPLETED for user
0 and exits bootanim normally at about 209 seconds. Setup Wizard launches,
but its WelcomeActivity suffers an input-dispatch ANR. Its RenderThread
waits in SwiftShader vk::Queue::waitIdle through ANGLE's swapBuffers path;
the UI thread waits for HWUI rendering. The trace establishes a rendering
wait, not whether it is a deadlock or slow software rendering. Bluetooth
also repeatedly aborts because its HCI HAL is absent, and configstore
continues receiving SIGSYS. No final reboot cause appears in this capture.

The maintainer still sees the kernel console rather than Android's GUI.
The kernel uses simpleDRM's 720x1560 bootloader framebuffer with fbcon and
`console=tty0`. Inspection in recovery confirms the framebuffer console is
bound; the previous normal-boot helper had no console handoff. The helper
now detaches only consoles named "frame buffer" at post-fs, before graphics
services start, to prevent kernel console output from overwriting Android.
Recovery does not run this helper. Kernel/logcat capture remains enabled.
This change needs a rebuilt vendor image and normal-boot validation; it
does not resolve the separate SwiftShader queue wait seen in Setup Wizard.

## Persistent boot diagnostics

Adapted from maintainer commit
`fc640de38ca3be31de7e3fd798884a89527b9a56` in
`vishwajithkv/android_device_xiaomi_laurel_sprout`. The system_ext
`laurel_boot_logger` starts at post-fs from the active device init script.
It waits for a real ext4 metadata mount and records five minutes of kernel,
logcat and mount/device-mapper state under `/metadata/boot-debug/boot-<UUID>`.
It creates a distinct directory per attempt and never removes previous logs.
It refuses to start capturing with less than 4 MiB free on metadata.
Kernel output is limited to 512 KiB and logcat rotates at 512 KiB with four
backups. Initial state is preserved separately; state.log keeps only the latest snapshot
instead of appending repeated listings. Snapshots flush metadata every two seconds;
kernel records use synchronous data writes to improve short-boot capture.

The init service is disabled/oneshot, explicitly started once at post-fs.
Its `su` label is for this permissive userdebug bringup only. It is not a
production SELinux integration. It does not read encryption key contents or
format/mount partitions. Capture cannot cover failures before the service
starts, an unavailable metadata mount, or storage lost during firmware reset.

After a failed boot, use recovery ADB to retrieve `/metadata/boot-debug`.
Recovery currently does not automatically mount metadata. For read-only
retrieval, the maintainer can mount it without journal replay:

```sh
adb shell mount -t ext4 -o ro,noload /dev/block/by-name/metadata /metadata
adb pull /metadata/boot-debug ./laurel-boot-debug
```

Check `/proc/mounts` first: skip the mount if metadata is already mounted.
Use the directory boot UUID and boot-info.txt, not wall-clock timestamps,
to identify attempts because this profile has no reliable RTC. Review
kernel.log, logcat.txt (and logcat.txt.1 through logcat.txt.4) and state.log together. The first logger captured a failed boot successfully. The next attempt produced
no new logs: metadata had only 4.8 MiB free, below the original 8 MiB guard.
The threshold is now 4 MiB and repeated state listings no longer grow the
state file (the first captured state.log was about 3 MiB). Existing logs are
preserved. This correction still needs maintainer build and boot validation.
The agent
has not built or run this logger. Rebuilt system/boot/vendor images and a
maintainer boot attempt are required before logs can exist.

## Human build

From the synced checkout (the dependencies are already copied here):

```sh
cd /home/vishwajithkv/android/lineage
source build/envsetup.sh
breakfast laurel_sprout userdebug
m bacon
```

`breakfast` selects this checkout's `bp4a` release automatically. Outputs go
to `out/target/product/laurel_sprout`. The boot image includes the newly built
kernel, appended DTB and recovery ramdisk; vendor includes its newly built
modules and selected HALs. Image size limits are enforced by the human build.
This command has not been run by the agent.

For a separate checkout, copy `local_manifests/laurel-mainline.xml` into
`.repo/local_manifests/` **before** syncing or running breakfast. The custom
kernel is declared there because roomservice's ordinary dependencies assume
LineageOS repository ownership.

## Maintainer checks after building

Inspect the generated kernel config for built-in UFS, USB gadget/configfs,
Binder, simpleDRM, PMIC thermal, F2FS security and encryption support. Confirm
the appended DTB, boot header, first-stage fstab files, module load list and
image sizes before the next boot experiment. Report build errors and kernel,
pstore/recovery, init, SurfaceFlinger and vold logs. A return to fastboot alone
does not identify whether failure occurred in the bootloader or kernel.

## References

- [LineageOS 24 Mi A3 fstab](https://github.com/LineageOS/android_device_xiaomi_laurel_sprout/blob/d389daebe22c2713b09742ab7153535c421ebdeb/rootdir/etc/fstab.qcom):
  system, vendor and metadata use first-stage mounting, as in this port.

- `device/mainline/common/docs/` in the documentation workspace.
- `kernel/mainline/sm6125-mainline/Documentation/android/FIRST_BOOT.md`.
- [postmarketOS Mi A3](https://wiki.postmarketos.org/wiki/Xiaomi_Mi_A3_%28xiaomi-laurel%29).
- [postmarketOS device recipe](https://gitlab.com/postmarketOS/pmaports/-/tree/master/device/testing/device-xiaomi-laurel).
