#!/bin/sh
# One-shot expansion of the project's known USB image, never a generic grow tool.
# The dispatcher must execute this script, not source it: redirection is local.
exec >/dev/null 2>&1
set -eu
export LC_ALL=C LANG=C

SYSFS_ROOT=/sys
DEV_ROOT=/dev
MARKER=/etc/eelinux/usb-rootfs-layout
SCRIPT_PATH=/expand-usb-rootfs.sh
HOOK_PATH=/etc/init.d/usb-root-expand
HOOK_LINK=/etc/rc.d/rcS.d/S19usb-root-expand
LOCK_FILE=/run/lock/eelinux-usb-root-expand.lock
DVD_MARKER=/.dvd/session
PRINTK_FILE=/proc/sys/kernel/printk
console_restore_needed=0
console_saved=
resize_pid=

# Keep these in step with hardware_usb.py and rootfs.py. The marker is emitted
# only by USB packaging; UUID/label and all MBR geometry are independently read.
EXPECTED_UUID=e30eebbb-9122-536d-b9b4-0950c22e8c24
BOOT_START=2048
BOOT_SECTORS=131072
ROOT_START=133120

decimal() {
    case "$1" in ''|*[!0-9]*|0[0-9]*) return 1 ;; esac
}

device_matches() {
    [ "$(readlink -f -- "$1")" = "$1" ] || return 1
    [ "$(stat -Lc %F -- "$1")" = 'block special file' ] || return 1
    device_hex=$(stat -Lc '%t:%T' -- "$1") || return 1
    case "$device_hex" in ''|*[!0-9a-fA-F:]*|:*|*:|*:*:*) return 1 ;; esac
    case "$device_hex" in *:*) ;; *) return 1 ;; esac
    [ "$((0x${device_hex%%:*})):$((0x${device_hex#*:}))" = "$2" ]
}

empty_holders() {
    [ -d "$1/holders" ] || return 1
    holders=$(find "$1/holders" -mindepth 1 -maxdepth 1 -print -quit) || return 1
    [ -z "$holders" ]
}

mounted_root() {
    root_mount=$(findmnt --raw --noheadings --mountpoint / --output MAJ:MIN,FSTYPE,OPTIONS) || return 1
    case "$root_mount" in *'
'*) return 1 ;; esac
    IFS=' ' read -r mounted_number mounted_type mounted_options mounted_extra <<EOF
$root_mount
EOF
    [ -z "$mounted_extra" ] && [ "$mounted_number" = "$root_number" ] && [ "$mounted_type" = ext4 ] || return 1
    case ",$mounted_options," in *,rw,*) ;; *) return 1 ;; esac
    case ",$mounted_options," in *,ro,*) return 1 ;; esac
    findmnt --raw --noheadings --output MAJ:MIN,TARGET | awk -v root="$root_number" -v disk="$disk_number" '
        $1 == root { if ($2 != "/" || NF != 2) bad=1; roots++ }
        $1 == disk { bad=1 }
        END { exit (bad || roots != 1) }
    '
}

table_size() {
    # Parse data, never shell-evaluate sfdisk output. -N later changes only slot2.
    table_dump=$(sfdisk --dump "$disk_device") || return 1
    printf '%s\n' "$table_dump" | awk -v disk="$disk_device" -v boot="$boot_device" -v root="$root_device" '
        /^label:/ { if ($2 != "dos") bad=1; label++ }
        /^label-id:/ { if (tolower($2) != "0x584c6565") bad=1; id++ }
        /^device:/ { if ($2 != disk) bad=1; device++ }
        /^unit:/ { if ($2 != "sectors") bad=1; unit++ }
        /^sector-size:/ { if ($2 != "512") bad=1; sector++ }
        /^\// {
            node=$1; if (node != boot && node != root) bad=1
            body=$0; sub(/^[^:]*:/,"",body); n=split(body,fields,",")
            start=""; size=""; type=""
            for (i=1;i<=n;i++) {
                field=fields[i]; gsub(/[[:space:]]/,"",field)
                if (field ~ /^start=[0-9]+$/) { if (start != "") bad=1; sub(/^start=/,"",field); start=field }
                else if (field ~ /^size=[0-9]+$/) { if (size != "") bad=1; sub(/^size=/,"",field); size=field }
                else if (field ~ /^type=(0x)?[0-9a-fA-F]+$/) { if (type != "") bad=1; sub(/^type=/,"",field); type=tolower(field) }
                else bad=1
            }
            if (node == boot) { boots++; if (start != 2048 || size != 131072 || type !~ /^(0x)?0*c$/) bad=1 }
            if (node == root) { roots++; if (start != 133120 || size < 131072 || type !~ /^(0x)?0*83$/) bad=1; root_size=size }
        }
        END {
            if (bad || label != 1 || id != 1 || device != 1 || unit != 1 || sector != 1 || boots != 1 || roots != 1) exit 1
            print root_size
        }
    '
}

filesystem_blocks() {
    [ "$(blkid -p -s TYPE -o value "$root_device")" = ext4 ] || return 1
    [ "$(blkid -p -s UUID -o value "$root_device")" = "$EXPECTED_UUID" ] || return 1
    [ "$(blkid -p -s LABEL -o value "$root_device")" = EELINUX_ROOT ] || return 1
    filesystem_header=$(dumpe2fs -h "$root_device") || return 1
    printf '%s\n' "$filesystem_header" | awk '
        /^Block count:/ { count=$3; counts++ }
        /^Block size:/ { size=$3; sizes++ }
        /^Filesystem features:/ { for (i=3;i<=NF;i++) if ($i == "resize_inode") resize=1 }
        END { if (counts != 1 || sizes != 1 || count !~ /^[0-9]+$/ || count < 1 || size != 4096 || !resize) exit 1; print count }
    '
}

console_values() {
    console_text=$(cat "$PRINTK_FILE") || return 1
    printf '%s\n' "$console_text" | awk '
        { lines++; if (NF != 4) bad=1; for(i=1;i<=NF;i++) if ($i !~ /^[0-9]+$/ || $i > 15) bad=1 }
        END { if (lines != 1 || bad) exit 1; print $1, $2, $3, $4 }
    '
}

restore_console() {
    [ "$console_restore_needed" = 1 ] || return 0
    printf '%s\n' "$console_saved" > "$PRINTK_FILE" || return 1
    [ "$(console_values)" = "$console_saved" ] || return 1
    console_restore_needed=0
}

interrupt_resize() {
    trap '' HUP INT TERM
    if [ -n "$resize_pid" ]; then
        kill "$resize_pid" || :
        wait "$resize_pid" || :
        resize_pid=
    fi
    restore_console || :
    exit 1
}

grow_filesystem() {
    # ext4 emits KERN_INFO itself; redirecting resize2fs cannot silence that.
    # This optional interface may be absent when CONFIG_SYSCTL is disabled.
    # Never let console control prevent growth or require a kernel change.
    console_saved=
    if [ -f "$PRINTK_FILE" ] && [ ! -L "$PRINTK_FILE" ]; then
        console_saved=$(console_values) || console_saved=
    fi
    trap 'restore_console || :' 0
    trap 'interrupt_resize' HUP INT TERM
    if [ -n "$console_saved" ]; then
        IFS=' ' read -r console_level message_level minimum_level default_level <<EOF
$console_saved
EOF
        if [ "$console_level" -gt 5 ] && [ -w "$PRINTK_FILE" ]; then
            console_restore_needed=1
            if ! printf '5 %s %s %s\n' "$message_level" "$minimum_level" "$default_level" > "$PRINTK_FILE" ||
                    [ "$(console_values)" != "5 $message_level $minimum_level $default_level" ]; then
                restore_console || :
            fi
        fi
    fi
    resize2fs "$root_device" &
    resize_pid=$!
    resize_status=0
    wait "$resize_pid" || resize_status=$?
    resize_pid=
    restore_console || :
    # Keep the EXIT restore as a final best-effort retry if an interface write
    # failed. Its status cannot mask the real filesystem result.
    trap - HUP INT TERM
    [ "$resize_status" = 0 ]
}

main() {
    [ "$(id -u)" = 0 ] || return 1
    [ ! -e "$DVD_MARKER" ] || return 1
    [ -f "$MARKER" ] && [ ! -L "$MARKER" ] || return 1
    [ "$(cat "$MARKER")" = 'eeLinux USB root expansion v1' ] || return 1
    marker_bytes=$(wc -c < "$MARKER")
    [ "$marker_bytes" -eq 30 ] || return 1
    [ -f "$SCRIPT_PATH" ] && [ ! -L "$SCRIPT_PATH" ] || return 1
    exec 9>"$LOCK_FILE"
    flock --nonblock 9 || return 1

    root_number=$(findmnt --raw --noheadings --mountpoint / --output MAJ:MIN) || return 1
    case "$root_number" in ''|*[!0-9:]*|:*|*:|*:*:*) return 1 ;; esac
    case "$root_number" in *:*) ;; *) return 1 ;; esac
    root_sys=$(readlink -f -- "$SYSFS_ROOT/dev/block/$root_number") || return 1
    case "$root_sys" in "$SYSFS_ROOT/devices/virtual/"*) return 1 ;; "$SYSFS_ROOT/devices/"*) ;; *) return 1 ;; esac
    [ "$(cat "$root_sys/partition")" = 2 ] || return 1
    [ "$(cat "$root_sys/dev")" = "$root_number" ] || return 1
    [ "$(cat "$root_sys/start")" = "$ROOT_START" ] || return 1
    disk_sys=${root_sys%/*};disk_name=${disk_sys##*/};root_name=${root_sys##*/}
    case "$disk_name$root_name" in ''|*[!a-zA-Z0-9._-]*) return 1 ;; esac
    disk_number=$(cat "$disk_sys/dev") || return 1
    [ "$(readlink -f -- "$SYSFS_ROOT/dev/block/$disk_number")" = "$disk_sys" ] || return 1
    root_device=$DEV_ROOT/$root_name;disk_device=$DEV_ROOT/$disk_name
    device_matches "$root_device" "$root_number" && device_matches "$disk_device" "$disk_number" || return 1
    [ "$(cat "$root_sys/ro")" = 0 ] && [ "$(cat "$disk_sys/ro")" = 0 ] || return 1
    empty_holders "$root_sys" && empty_holders "$disk_sys" || return 1

    usb_bus=$(readlink -f -- "$SYSFS_ROOT/bus/usb") || return 1
    [ -d "$usb_bus" ] || return 1
    ancestor=$disk_sys;usb=0
    while [ "$ancestor" != "$SYSFS_ROOT" ] && [ "$ancestor" != / ]; do
        if [ -L "$ancestor/subsystem" ] && [ "$(readlink -f -- "$ancestor/subsystem")" = "$usb_bus" ]; then usb=1;break;fi
        ancestor=${ancestor%/*}
    done
    [ "$usb" = 1 ] || return 1
    partition_count=0;boot_device=
    for partition_file in "$disk_sys"/*/partition; do
        [ -f "$partition_file" ] || continue
        partition_sys=${partition_file%/*};partition_index=$(cat "$partition_file")
        partition_count=$((partition_count+1))
        case "$partition_index" in
            1)
                [ "$(cat "$partition_sys/start")" = "$BOOT_START" ] && [ "$(cat "$partition_sys/size")" = "$BOOT_SECTORS" ] || return 1
                boot_device=$DEV_ROOT/${partition_sys##*/}
                device_matches "$boot_device" "$(cat "$partition_sys/dev")" || return 1
                ;;
            2) [ "$partition_sys" = "$root_sys" ] || return 1 ;;
            *) return 1 ;;
        esac
    done
    [ "$partition_count" = 2 ] && [ -n "$boot_device" ] || return 1
    mounted_root || return 1
    [ "$(blockdev --getss "$disk_device")" = 512 ] || return 1
    disk_sectors=$(blockdev --getsz "$disk_device") || return 1
    decimal "$disk_sectors" && [ "$disk_sectors" -le 4294967296 ] || return 1
    target_sectors=$((disk_sectors-ROOT_START))
    [ "$target_sectors" -ge 131072 ] || return 1
    current_sectors=$(table_size) || return 1
    decimal "$current_sectors" && [ "$current_sectors" -le "$target_sectors" ] || return 1
    kernel_sectors=$(cat "$root_sys/size") || return 1
    decimal "$kernel_sectors" && [ "$kernel_sectors" -le "$current_sectors" ] || return 1
    [ "$(blockdev --getsz "$root_device")" = "$kernel_sectors" ] || return 1
    old_blocks=$(filesystem_blocks) || return 1
    decimal "$old_blocks" && [ "$old_blocks" -le "$((kernel_sectors/8))" ] || return 1
    target_blocks=$((target_sectors/8))

    # Save the boot code, disk ID and complete partition1 entry, not its FAT data.
    # Numeric sector sizes avoid sfdisk alignment changes. No wipe, move, shrink,
    # whole-device reread, filesystem check or formatting operation is permitted.
    prefix_before=$(od -An -v -tx1 -N462 -- "$disk_device") || return 1
    device_matches "$root_device" "$root_number" && device_matches "$disk_device" "$disk_number" || return 1
    mounted_root && [ "$(blockdev --getsz "$disk_device")" = "$disk_sectors" ] || return 1
    if [ "$current_sectors" -lt "$target_sectors" ]; then
        printf 'start=%s, size=%s\n' "$ROOT_START" "$target_sectors" |
            sfdisk --lock=nonblock --no-reread --no-tell-kernel --wipe never --wipe-partitions never --partno 2 "$disk_device" || return 1
    fi
    [ "$(table_size)" = "$target_sectors" ] || return 1
    [ "$(od -An -v -tx1 -N462 -- "$disk_device")" = "$prefix_before" ] || return 1
    if [ "$kernel_sectors" -ne "$target_sectors" ]; then
        partx --update --nr 2 "$disk_device" || return 1
    fi
    [ "$(cat "$root_sys/start")" = "$ROOT_START" ] && [ "$(cat "$root_sys/size")" = "$target_sectors" ] || return 1
    [ "$(blockdev --getsz "$root_device")" = "$target_sectors" ] || return 1
    mounted_root || return 1
    if [ "$old_blocks" -lt "$target_blocks" ]; then
        grow_filesystem || return 1
    fi
    [ "$(filesystem_blocks)" = "$target_blocks" ] || return 1
    [ "$(table_size)" = "$target_sectors" ] && [ "$(blockdev --getsz "$disk_device")" = "$disk_sectors" ] || return 1
    [ "$(od -An -v -tx1 -N462 -- "$disk_device")" = "$prefix_before" ] || return 1
    device_matches "$root_device" "$root_number" && mounted_root || return 1

    # Keep every retry mechanism until all on-disk, kernel and filesystem checks
    # pass. The caller always treats failure as an optional silent boot task.
    rm -f -- "$HOOK_LINK" "$HOOK_PATH" "$MARKER" "$SCRIPT_PATH"
}

main
