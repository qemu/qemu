#!/usr/bin/env sh
#
# Copyright (C) 2025 Red Hat, Inc.
#
# Based on rust_to_clang_target() tests from rust-bindgen.
#
# SPDX-License-Identifier: GPL-2.0-or-later

scripts_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$scripts_dir/rust-to-clang-target.sh"

test_case() {
    input="$1"
    expected="$2"
    result=$(rust_to_clang_target "$input")

    if [ "$result" = "$expected" ]; then
        echo " OK: '$input' -> '$result'"
    else
        echo " FAILED: '$input'"
        echo "  Expected: '$expected'"
        echo "  Got:      '$result'"
        exit 1
    fi
}

echo "Running tests..."

test_case "aarch64-apple-ios" "arm64-apple-ios"
test_case "riscv64gc-unknown-linux-gnu" "riscv64-unknown-linux-gnu"
test_case "riscv64imac-unknown-none-elf" "riscv64-unknown-none-elf"
test_case "riscv32imc-unknown-none-elf" "riscv32-unknown-none-elf"
test_case "riscv32imac-unknown-none-elf" "riscv32-unknown-none-elf"
test_case "riscv32imafc-unknown-none-elf" "riscv32-unknown-none-elf"
test_case "riscv32i-unknown-none-elf" "riscv32-unknown-none-elf"
test_case "riscv32imc-esp-espidf" "riscv32-esp-elf"
test_case "xtensa-esp32-espidf" "xtensa-esp32-elf"
test_case "aarch64-apple-ios-sim" "arm64-apple-ios-simulator"
test_case "aarch64-apple-tvos-sim" "arm64-apple-tvos-simulator"
test_case "aarch64-apple-watchos-sim" "arm64-apple-watchos-simulator"

echo ""
echo "All tests passed!"
