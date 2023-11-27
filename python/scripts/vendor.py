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
        "meson==1.2.3":
        "4533a43c34548edd1f63a276a42690fce15bde9409bcf20c4b8fa3d7e4d7cac1",

        "tomli==2.0.1":
        "939de3e7a6161af0c887ef91b7d41a53e7c5a1ca976325f429cb46ea9bc30ecc",
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
