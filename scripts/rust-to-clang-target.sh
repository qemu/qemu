# Copyright (C) 2025 Red Hat, Inc.
#
# Based on rust_to_clang_target() from rust-bindgen.
#
# SPDX-License-Identifier: GPL-2.0-or-later

rust_to_clang_target() {
    rust_target="$1"

    # Split the string by hyphens
    triple_parts=""
    old_IFS="$IFS"
    IFS='-'
    for part in $rust_target; do
        triple_parts="$triple_parts $part"
    done
    IFS="$old_IFS"
    set -- $triple_parts

    # RISC-V
    case "$1" in
        riscv32*)
            set -- "riscv32" "${2}" "${3}" "${4}"
            ;;
        riscv64*)
            set -- "riscv64" "${2}" "${3}" "${4}"
            ;;
    esac

    # Apple
    if [ "$2" = "apple" ]; then
        if [ "$1" = "aarch64" ]; then
            set -- "arm64" "${2}" "${3}" "${4}"
        fi
        if [ "$4" = "sim" ]; then
            set -- "${1}" "${2}" "${3}" "simulator"
        fi
    fi

    # ESP-IDF
    if [ "$3" = "espidf" ]; then
        set -- "${1}" "${2}" "elf" "${4}"
    fi

    # Reassemble the string
    new_triple=""
    first=1
    for part in "$@"; do
        if [ -n "$part" ]; then
            if [ "$first" -eq 1 ]; then
                new_triple="$part"
                first=0
            else
                new_triple="$new_triple-$part"
            fi
        fi
    done

    echo "$new_triple"
}
