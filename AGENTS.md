# AGENTS.md - Mi A3 mainline ROM device tree

Agents must read this file before touching this tree.

- Device: Xiaomi Mi A3, laurel_sprout; SoC: SM6125 / Snapdragon 665 / Trinket.
- This is the existing ROM tree, converted at the maintainer's explicit request.
- Read README.md and the kernel Documentation/android/FIRST_BOOT.md first.
- Follow device/mainline/common/docs/ and hardware/mainline/common/docs/ when present.
- Do not build, compile, flash, run tests, format data or repartition.
- Search precise source directories, never the Android tree root.
- Keep physical A/B partition sizes and bootloader configuration documented.
- Keep UFS, USB, CPU and power dependencies built in; optional hardware remains disabled.
- Do not inherit the downstream sm6125-common or proprietary hardware HALs.
- Package newly built kernel modules; never package previous-build reference binaries.
- No boot, display or decryption claim is validated before maintainer logs confirm it.
