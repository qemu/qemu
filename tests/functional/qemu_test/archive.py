# SPDX-License-Identifier: GPL-2.0-or-later
#
# Utilities for python-based QEMU tests
#
# Copyright 2024 Red Hat, Inc.
#
# Authors:
#  Thomas Huth <thuth@redhat.com>

import os
from subprocess import check_call, run, DEVNULL
import tarfile
import zipfile

from .cmd import run_cmd


def tar_extract(archive, dest_dir, member=None):
    with tarfile.open(archive) as tf:
        if hasattr(tarfile, 'data_filter'):
            tf.extraction_filter = getattr(tarfile, 'data_filter',
                                           (lambda member, path: member))
        if member:
            tf.extract(member=member, path=dest_dir)
        else:
            tf.extractall(path=dest_dir)

def cpio_extract(archive, output_path):
    cwd = os.getcwd()
    os.chdir(output_path)
    # Not passing 'check=True' as cpio exits with non-zero
    # status if the archive contains any device nodes :-(
    if type(archive) == str:
        run(['cpio', '-i', '-F', archive],
            stdout=DEVNULL, stderr=DEVNULL)
    else:
        run(['cpio', '-i'],
            input=archive.read(),
            stdout=DEVNULL, stderr=DEVNULL)
    os.chdir(cwd)

def zip_extract(archive, dest_dir, member=None):
    with zipfile.ZipFile(archive, 'r') as zf:
        if member:
            zf.extract(member=member, path=dest_dir)
        else:
            zf.extractall(path=dest_dir)

def deb_extract(archive, dest_dir, member=None):
    cwd = os.getcwd()
    os.chdir(dest_dir)
    try:
        (stdout, stderr, ret) = run_cmd(['ar', 't', archive])
        file_path = stdout.split()[2]
        run_cmd(['ar', 'x', archive, file_path])
        tar_extract(file_path, dest_dir, member)
    finally:
        os.chdir(cwd)
