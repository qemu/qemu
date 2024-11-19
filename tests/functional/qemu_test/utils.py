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
import subprocess
import tarfile

"""
Round up to next power of 2
"""
def pow2ceil(x):
    return 1 if x == 0 else 2**(x - 1).bit_length()

def file_truncate(path, size):
    if size != os.path.getsize(path):
        with open(path, 'ab+') as fd:
            fd.truncate(size)

"""
Expand file size to next power of 2
"""
def image_pow2ceil_expand(path):
        size = os.path.getsize(path)
        size_aligned = pow2ceil(size)
        if size != size_aligned:
            with open(path, 'ab+') as fd:
                fd.truncate(size_aligned)

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

def cpio_extract(cpio_handle, output_path):
    cwd = os.getcwd()
    os.chdir(output_path)
    subprocess.run(['cpio', '-i'],
                   input=cpio_handle.read(),
                   stderr=subprocess.DEVNULL)
    os.chdir(cwd)
