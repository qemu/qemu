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
from urllib.parse import urlparse
import zipfile

from .asset import Asset


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
        proc = run(['ar', 't', archive],
                   check=True, capture_output=True, encoding='utf8')
        file_path = proc.stdout.split()[2]
        check_call(['ar', 'x', archive, file_path],
                   stdout=DEVNULL, stderr=DEVNULL)
        tar_extract(file_path, dest_dir, member)
    finally:
        os.chdir(cwd)

'''
@params archive: filename, Asset, or file-like object to extract
@params dest_dir: target directory to extract into
@params member: optional member file to limit extraction to

Extracts @archive into @dest_dir. All files are extracted
unless @member specifies a limit.

If @format is None, heuristics will be applied to guess the format
from the filename or Asset URL. @format must be non-None if @archive
is a file-like object.
'''
def archive_extract(archive, dest_dir, format=None, member=None):
    if format is None:
        format = guess_archive_format(archive)
    if type(archive) == Asset:
        archive = str(archive)

    if format == "tar":
        tar_extract(archive, dest_dir, member)
    elif format == "zip":
        zip_extract(archive, dest_dir, member)
    elif format == "cpio":
        if member is not None:
            raise Exception("Unable to filter cpio extraction")
        cpio_extract(archive, dest_dir)
    elif format == "deb":
        if type(archive) != str:
            raise Exception("Unable to use file-like object with deb archives")
        deb_extract(archive, dest_dir, "./" + member)
    else:
        raise Exception(f"Unknown archive format {format}")

'''
@params archive: filename, or Asset to guess

Guess the format of @compressed, raising an exception if
no format can be determined
'''
def guess_archive_format(archive):
    if type(archive) == Asset:
        archive = urlparse(archive.url).path
    elif type(archive) != str:
        raise Exception(f"Unable to guess archive format for {archive}")

    if ".tar." in archive or archive.endswith("tgz"):
        return "tar"
    elif archive.endswith(".zip"):
        return "zip"
    elif archive.endswith(".cpio"):
        return "cpio"
    elif archive.endswith(".deb") or archive.endswith(".udeb"):
        return "deb"
    else:
        raise Exception(f"Unknown archive format for {archive}")
