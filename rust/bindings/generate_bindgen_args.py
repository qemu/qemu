#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""
Generate bindgen arguments from Cargo.toml metadata for QEMU's Rust FFI bindings.

Author: Paolo Bonzini <pbonzini@redhat.com>

Copyright (C) 2025 Red Hat, Inc.

This script processes Cargo.toml file for QEMU's bindings crates (util-sys,
chardev-sys, qom-sys, etc.); it generates bindgen command lines that allow
easy customization and that export the right headers in each bindings crate.

For detailed information, see docs/devel/rust.rst.
"""

import os
import re
import sys
import argparse
from pathlib import Path
from dataclasses import dataclass
from typing import Iterable, List, Dict, Any

try:
    import tomllib
except ImportError:
    import tomli as tomllib  # type: ignore

INCLUDE_RE = re.compile(r'^#include\s+"([^"]+)"')
OPTIONS = [
    "bitfield-enum",
    "newtype-enum",
    "newtype-global-enum",
    "rustified-enum",
    "rustified-non-exhaustive-enum",
    "constified-enum",
    "constified-enum-module",
    "normal-alias",
    "new-type-alias",
    "new-type-alias-deref",
    "bindgen-wrapper-union",
    "manually-drop-union",
    "blocklist-type",
    "blocklist-function",
    "blocklist-item",
    "blocklist-file",
    "blocklist-var",
    "opaque-type",
    "no-partialeq",
    "no-copy",
    "no-debug",
    "no-default",
    "no-hash",
    "must-use-type",
    "with-derive-custom",
    "with-derive-custom-struct",
    "with-derive-custom-enum",
    "with-derive-custom-union",
    "with-attribute-custom",
    "with-attribute-custom-struct",
    "with-attribute-custom-enum",
    "with-attribute-custom-union",
]


@dataclass
class BindgenInfo:
    cmd_args: List[str]
    inputs: List[str]


def extract_includes(lines: Iterable[str]) -> List[str]:
    """Extract #include directives from a file."""
    includes: List[str] = []
    for line in lines:
        match = INCLUDE_RE.match(line.strip())
        if match:
            includes.append(match.group(1))
    return includes


def build_bindgen_args(metadata: Dict[str, Any]) -> List[str]:
    """Build command line arguments from [package.metadata.bindgen]."""
    args: List[str] = []
    for key, values in metadata.items():
        if key in OPTIONS:
            flag = f"--{key}"
            assert isinstance(values, list)
            for value in values:
                args.append(flag)
                args.append(value)

    return args


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate bindgen arguments from Cargo.toml metadata"
    )
    parser.add_argument(
        "directories", nargs="+", help="Directories containing Cargo.toml files"
    )
    parser.add_argument(
        "-I",
        "--include-root",
        default=None,
        help="Base path for --allowlist-file/--blocklist-file",
    )
    parser.add_argument("--source-dir", default=os.getcwd(), help="Source directory")
    parser.add_argument("-o", "--output", required=True, help="Output file")
    parser.add_argument("--dep-file", help="Dependency file to write")
    args = parser.parse_args()

    prev_allowlist_files: Dict[str, object] = {}
    bindgen_infos: Dict[str, BindgenInfo] = {}

    os.chdir(args.source_dir)
    include_root = args.include_root or args.source_dir
    for directory in args.directories:
        cargo_path = Path(directory) / "Cargo.toml"
        inputs = [str(Path(args.source_dir) / cargo_path)]

        with open(cargo_path, "rb") as f:
            cargo_toml = tomllib.load(f)

        metadata = cargo_toml.get("package", {}).get("metadata", {}).get("bindgen", {})
        input_file = Path(directory) / metadata["header"]
        inputs.append(str(Path(args.source_dir) / input_file))

        cmd_args = build_bindgen_args(metadata)

        # Each include file is allowed for this file and blocked in the
        # next ones
        for blocklist_path in prev_allowlist_files:
            cmd_args.extend(["--blocklist-file", blocklist_path])
        with open(input_file, "r", encoding="utf-8", errors="ignore") as f:
            includes = extract_includes(f)
        for allowlist_file in includes + metadata.get("additional-files", []):
            allowlist_path = Path(include_root) / allowlist_file
            cmd_args.extend(["--allowlist-file", str(allowlist_path)])
            prev_allowlist_files.setdefault(str(allowlist_path), True)

        bindgen_infos[directory] = BindgenInfo(cmd_args=cmd_args, inputs=inputs)

    # now write the output
    with open(args.output, "w") as f:
        for directory, info in bindgen_infos.items():
            args_sh = " ".join(info.cmd_args)
            f.write(f"{directory}={args_sh}\n")

    if args.dep_file:
        with open(args.dep_file, "w") as f:
            deps: List[str] = []
            for info in bindgen_infos.values():
                deps += info.inputs
            f.write(f"{os.path.basename(args.output)}: {' '.join(deps)}\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
