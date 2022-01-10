"""
QEMU Object Model FUSE filesystem tool

This script offers a simple FUSE filesystem within which the QOM tree
may be browsed, queried and edited using traditional shell tooling.

This script requires the 'fusepy' python package.


usage: qom-fuse [-h] [--socket SOCKET] <mount>

Mount a QOM tree as a FUSE filesystem

positional arguments:
  <mount>               Mount point

optional arguments:
  -h, --help            show this help message and exit
  --socket SOCKET, -s SOCKET
                        QMP socket path or address (addr:port). May also be
                        set via QMP_SOCKET environment variable.
"""
##
# Copyright IBM, Corp. 2012
# Copyright (C) 2020 Red Hat, Inc.
#
# Authors:
#  Anthony Liguori   <aliguori@us.ibm.com>
#  Markus Armbruster <armbru@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
##

import argparse
from errno import ENOENT, EPERM
import stat
import sys
from typing import (
    IO,
    Dict,
    Iterator,
    Mapping,
    Optional,
    Union,
)

import fuse
from fuse import FUSE, FuseOSError, Operations

from qemu.aqmp import ExecuteError

from .qom_common import QOMCommand


fuse.fuse_python_api = (0, 2)


class QOMFuse(QOMCommand, Operations):
    """
    QOMFuse implements both fuse.Operations and QOMCommand.

    Operations implements the FS, and QOMCommand implements the CLI command.
    """
    name = 'fuse'
    help = 'Mount a QOM tree as a FUSE filesystem'
    fuse: FUSE

    @classmethod
    def configure_parser(cls, parser: argparse.ArgumentParser) -> None:
        super().configure_parser(parser)
        parser.add_argument(
            'mount',
            metavar='<mount>',
            action='store',
            help="Mount point",
        )

    def __init__(self, args: argparse.Namespace):
        super().__init__(args)
        self.mount = args.mount
        self.ino_map: Dict[str, int] = {}
        self.ino_count = 1

    def run(self) -> int:
        print(f"Mounting QOMFS to '{self.mount}'", file=sys.stderr)
        self.fuse = FUSE(self, self.mount, foreground=True)
        return 0

    def get_ino(self, path: str) -> int:
        """Get an inode number for a given QOM path."""
        if path in self.ino_map:
            return self.ino_map[path]
        self.ino_map[path] = self.ino_count
        self.ino_count += 1
        return self.ino_map[path]

    def is_object(self, path: str) -> bool:
        """Is the given QOM path an object?"""
        try:
            self.qom_list(path)
            return True
        except ExecuteError:
            return False

    def is_property(self, path: str) -> bool:
        """Is the given QOM path a property?"""
        path, prop = path.rsplit('/', 1)
        if path == '':
            path = '/'
        try:
            for item in self.qom_list(path):
                if item.name == prop:
                    return True
            return False
        except ExecuteError:
            return False

    def is_link(self, path: str) -> bool:
        """Is the given QOM path a link?"""
        path, prop = path.rsplit('/', 1)
        if path == '':
            path = '/'
        try:
            for item in self.qom_list(path):
                if item.name == prop and item.link:
                    return True
            return False
        except ExecuteError:
            return False

    def read(self, path: str, size: int, offset: int, fh: IO[bytes]) -> bytes:
        if not self.is_property(path):
            raise FuseOSError(ENOENT)

        path, prop = path.rsplit('/', 1)
        if path == '':
            path = '/'
        try:
            data = str(self.qmp.command('qom-get', path=path, property=prop))
            data += '\n'  # make values shell friendly
        except ExecuteError as err:
            raise FuseOSError(EPERM) from err

        if offset > len(data):
            return b''

        return bytes(data[offset:][:size], encoding='utf-8')

    def readlink(self, path: str) -> Union[bool, str]:
        if not self.is_link(path):
            return False
        path, prop = path.rsplit('/', 1)
        prefix = '/'.join(['..'] * (len(path.split('/')) - 1))
        return prefix + str(self.qmp.command('qom-get', path=path,
                                             property=prop))

    def getattr(self, path: str,
                fh: Optional[IO[bytes]] = None) -> Mapping[str, object]:
        if self.is_link(path):
            value = {
                'st_mode': 0o755 | stat.S_IFLNK,
                'st_ino': self.get_ino(path),
                'st_dev': 0,
                'st_nlink': 2,
                'st_uid': 1000,
                'st_gid': 1000,
                'st_size': 4096,
                'st_atime': 0,
                'st_mtime': 0,
                'st_ctime': 0
            }
        elif self.is_object(path):
            value = {
                'st_mode': 0o755 | stat.S_IFDIR,
                'st_ino': self.get_ino(path),
                'st_dev': 0,
                'st_nlink': 2,
                'st_uid': 1000,
                'st_gid': 1000,
                'st_size': 4096,
                'st_atime': 0,
                'st_mtime': 0,
                'st_ctime': 0
            }
        elif self.is_property(path):
            value = {
                'st_mode': 0o644 | stat.S_IFREG,
                'st_ino': self.get_ino(path),
                'st_dev': 0,
                'st_nlink': 1,
                'st_uid': 1000,
                'st_gid': 1000,
                'st_size': 4096,
                'st_atime': 0,
                'st_mtime': 0,
                'st_ctime': 0
            }
        else:
            raise FuseOSError(ENOENT)
        return value

    def readdir(self, path: str, fh: IO[bytes]) -> Iterator[str]:
        yield '.'
        yield '..'
        for item in self.qom_list(path):
            yield item.name
