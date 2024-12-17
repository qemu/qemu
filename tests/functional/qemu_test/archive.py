# SPDX-License-Identifier: GPL-2.0-or-later
#
# Utilities for python-based QEMU tests
#
# Copyright 2024 Red Hat, Inc.
#
# Authors:
#  Thomas Huth <thuth@redhat.com>

import os
import subprocess
import tarfile


def tar_extract(archive, dest_dir, member=None):
    with tarfile.open(archive) as tf:
        if hasattr(tarfile, 'data_filter'):
            tf.extraction_filter = getattr(tarfile, 'data_filter',
                                           (lambda member, path: member))
        if member:
            tf.extract(member=member, path=dest_dir)
        else:
            tf.extractall(path=dest_dir)

def cpio_extract(cpio_handle, output_path):
    cwd = os.getcwd()
    os.chdir(output_path)
    subprocess.run(['cpio', '-i'],
                   input=cpio_handle.read(),
                   stderr=subprocess.DEVNULL)
    os.chdir(cwd)
