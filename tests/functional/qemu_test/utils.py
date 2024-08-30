# Utilities for python-based QEMU tests
#
# Copyright 2024 Red Hat, Inc.
#
# Authors:
#  Thomas Huth <thuth@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import gzip
import lzma
import os
import shutil
import tarfile

def archive_extract(archive, dest_dir, member=None):
    with tarfile.open(archive) as tf:
        if hasattr(tarfile, 'data_filter'):
            tf.extraction_filter = getattr(tarfile, 'data_filter',
                                           (lambda member, path: member))
        if member:
            tf.extract(member=member, path=dest_dir)
        else:
            tf.extractall(path=dest_dir)

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
