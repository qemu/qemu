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
import stat
import shutil
from urllib.parse import urlparse
from subprocess import run, CalledProcessError

from .asset import Asset


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


def zstd_uncompress(zstd_path, output_path):
    if os.path.exists(output_path):
        return

    try:
        run(['zstd', "-f", "-d", zstd_path,
             "-o", output_path], capture_output=True, check=True)
    except CalledProcessError as e:
        os.remove(output_path)
        raise Exception(
            f"Unable to decompress zstd file {zstd_path} with {e}") from e

    # zstd copies source archive permissions for the output
    # file, so must make this writable for QEMU
    os.chmod(output_path, stat.S_IRUSR | stat.S_IWUSR)


def uncompress(compressed, uncompressed, format=None):
    '''
    @params compressed: filename, Asset, or file-like object to uncompress
    @params uncompressed: filename to uncompress into
    @params format: optional compression format (gzip, lzma)

    Uncompresses @compressed into @uncompressed

    If @format is None, heuristics will be applied to guess the
    format from the filename or Asset URL. @format must be non-None
    if @uncompressed is a file-like object.

    Returns the fully qualified path to the uncompessed file
    '''
    if format is None:
        format = guess_uncompress_format(compressed)

    if format == "xz":
        lzma_uncompress(str(compressed), uncompressed)
    elif format == "gz":
        gzip_uncompress(str(compressed), uncompressed)
    elif format == "zstd":
        zstd_uncompress(str(compressed), uncompressed)
    else:
        raise Exception(f"Unknown compression format {format}")

def guess_uncompress_format(compressed):
    '''
    @params compressed: filename, Asset, or file-like object to guess

    Guess the format of @compressed, raising an exception if
    no format can be determined
    '''
    if isinstance(compressed, Asset):
        compressed = urlparse(compressed.url).path
    elif not isinstance(compressed, str):
        raise Exception(f"Unable to guess compression cformat for {compressed}")

    (_name, ext) = os.path.splitext(compressed)
    if ext == ".xz":
        return "xz"
    elif ext == ".gz":
        return "gz"
    elif ext in [".zstd", ".zst"]:
        return 'zstd'
    else:
        raise Exception(f"Unknown compression format for {compressed}")
