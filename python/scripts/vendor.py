#!/usr/bin/env python3
"""
vendor - QEMU python vendoring utility

usage: vendor [-h]

QEMU python vendoring utility

options:
  -h, --help  show this help message and exit
"""

# Copyright (C) 2023 Red Hat, Inc.
#
# Authors:
#  John Snow <jsnow@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> int:
    """Run the vendoring utility. See module-level docstring."""
    loud = False
    if os.environ.get("DEBUG") or os.environ.get("V"):
        loud = True

    # No options or anything for now, but I guess
    # you'll figure that out when you run --help.
    parser = argparse.ArgumentParser(
        prog="vendor",
        description="QEMU python vendoring utility",
    )
    parser.parse_args()

    packages = {
        "meson==1.9.0":
        "45e51ddc41e37d961582d06e78c48e0f9039011587f3495c4d6b0781dad92357",
    }

    vendor_dir = Path(__file__, "..", "..", "wheels").resolve()

    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8") as file:
        for dep_spec, checksum in packages.items():
            print(f"{dep_spec} --hash=sha256:{checksum}", file=file)
        file.flush()

        cli_args = [
            "pip",
            "download",
            "--dest",
            str(vendor_dir),
            "--require-hashes",
            "-r",
            file.name,
        ]
        if loud:
            cli_args.append("-v")

        print(" ".join(cli_args))
        subprocess.run(cli_args, check=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
