#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
"""Delete stale assets from the download cache of the functional tests"""

import os
import stat
import sys
import time
from pathlib import Path


cache_dir_env = os.getenv('QEMU_TEST_CACHE_DIR')
if cache_dir_env:
    cache_dir = Path(cache_dir_env, "download")
else:
    cache_dir = Path(Path("~").expanduser(), ".cache", "qemu", "download")

if not cache_dir.exists():
    print(f"Cache dir {cache_dir} does not exist!", file=sys.stderr)
    sys.exit(1)

os.chdir(cache_dir)

for file in cache_dir.iterdir():
    # Only consider the files that use a sha256 as filename:
    if len(file.name) != 64:
        continue

    try:
        timestamp = int(file.with_suffix(".stamp").read_text())
    except FileNotFoundError:
        # Assume it's an old file that was already in the cache before we
        # added the code for evicting stale assets. Use the release date
        # of QEMU v10.1 as a default timestamp.
        timestamp = time.mktime((2025, 8, 26, 0, 0, 0, 0, 0, 0))

    age = time.time() - timestamp

    # Delete files older than half of a year (183 days * 24h * 60m * 60s)
    if age > 15811200:
        print(f"Removing {cache_dir}/{file.name}.")
        file.chmod(stat.S_IWRITE)
        file.unlink()
