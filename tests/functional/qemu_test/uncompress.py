# SPDX-License-Identifier: GPL-2.0-or-later
#
# Utilities for python-based QEMU tests
#
# Copyright 2024 Red Hat, Inc.
#
# Authors:
#  Thomas Huth <thuth@redhat.com>

import gzip
import lzma
import os
import shutil


def gzip_uncompress(gz_path, output_path):
    if os.path.exists(output_path):
        return
    with gzip.open(gz_path, 'rb') as gz_in:
        try:
            with open(output_path, 'wb') as raw_out:
                shutil.copyfileobj(gz_in, raw_out)
        except:
            os.remove(output_path)
            raise

def lzma_uncompress(xz_path, output_path):
    if os.path.exists(output_path):
        return
    with lzma.open(xz_path, 'rb') as lzma_in:
        try:
            with open(output_path, 'wb') as raw_out:
                shutil.copyfileobj(lzma_in, raw_out)
        except:
            os.remove(output_path)
            raise
