#!/vendor/bin/sh
# SPDX-License-Identifier: Apache-2.0

# simpleDRM shares the bootloader framebuffer with fbcon. Stop the kernel
# console from drawing over Android before the graphics services start.
# This helper runs only from normal Android init; recovery keeps its console.
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
