#!/vendor/bin/sh
# SPDX-License-Identifier: Apache-2.0

# The current kernel disables fbcon, retaining DRM/fbdev for Android/recovery.
# Also detach fbcon when using an older matching bringup kernel: its console
# shares the bootloader framebuffer with simpleDRM. This is normal boot only.
for vtconsole in /sys/class/vtconsole/vtcon*; do
    [ -r "$vtconsole/name" ] || continue
    case "$(cat "$vtconsole/name")" in
        *"frame buffer"*)
            if ! echo 0 > "$vtconsole/bind"; then
                echo "laurel mainline: failed to detach $vtconsole" >&2
            fi
            ;;
    esac
done

# Select the controller exported by the kernel, before the USB HAL starts.
for controller in /sys/class/udc/*; do
    [ -d "$controller" ] || continue
    setprop sys.usb.controller "${controller##*/}"
    break
done
