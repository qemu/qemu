"""
QEMU Object Model testing tools.

usage: qom [-h] {set,get,list,tree,fuse} ...

Query and manipulate QOM data

optional arguments:
  -h, --help           show this help message and exit

QOM commands:
  {set,get,list,tree,fuse}
    set                Set a QOM property value
    get                Get a QOM property value
    list               List QOM properties at a given path
    tree               Show QOM tree from a given path
    fuse               Mount a QOM tree as a FUSE filesystem
"""
##
# Copyright John Snow 2020, for Red Hat, Inc.
# Copyright IBM, Corp. 2011
#
# Authors:
#  John Snow <jsnow@redhat.com>
#  Anthony Liguori <aliguori@amazon.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# Based on ./scripts/qmp/qom-[set|get|tree|list]
##

import argparse

from . import QMPResponseError
from .qom_common import QOMCommand


try:
    from .qom_fuse import QOMFuse
except ModuleNotFoundError as _err:
    if _err.name != 'fuse':
        raise
else:
    assert issubclass(QOMFuse, QOMCommand)


class QOMSet(QOMCommand):
    """
    QOM Command - Set a property to a given value.

    usage: qom-set [-h] [--socket SOCKET] <path>.<property> <value>

    Set a QOM property value

    positional arguments:
      <path>.<property>     QOM path and property, separated by a period '.'
      <value>               new QOM property value

    optional arguments:
      -h, --help            show this help message and exit
      --socket SOCKET, -s SOCKET
                            QMP socket path or address (addr:port). May also be
                            set via QMP_SOCKET environment variable.
    """
    name = 'set'
    help = 'Set a QOM property value'

    @classmethod
    def configure_parser(cls, parser: argparse.ArgumentParser) -> None:
        super().configure_parser(parser)
        cls.add_path_prop_arg(parser)
        parser.add_argument(
            'value',
            metavar='<value>',
            action='store',
            help='new QOM property value'
        )

    def __init__(self, args: argparse.Namespace):
        super().__init__(args)
        self.path, self.prop = args.path_prop.rsplit('.', 1)
        self.value = args.value

    def run(self) -> int:
        rsp = self.qmp.command(
            'qom-set',
            path=self.path,
            property=self.prop,
            value=self.value
        )
        print(rsp)
        return 0


class QOMGet(QOMCommand):
    """
    QOM Command - Get a property's current value.

    usage: qom-get [-h] [--socket SOCKET] <path>.<property>

    Get a QOM property value

    positional arguments:
      <path>.<property>     QOM path and property, separated by a period '.'

    optional arguments:
      -h, --help            show this help message and exit
      --socket SOCKET, -s SOCKET
                            QMP socket path or address (addr:port). May also be
                            set via QMP_SOCKET environment variable.
    """
    name = 'get'
    help = 'Get a QOM property value'

    @classmethod
    def configure_parser(cls, parser: argparse.ArgumentParser) -> None:
        super().configure_parser(parser)
        cls.add_path_prop_arg(parser)

    def __init__(self, args: argparse.Namespace):
        super().__init__(args)
        try:
            tmp = args.path_prop.rsplit('.', 1)
        except ValueError as err:
            raise ValueError('Invalid format for <path>.<property>') from err
        self.path = tmp[0]
        self.prop = tmp[1]

    def run(self) -> int:
        rsp = self.qmp.command(
            'qom-get',
            path=self.path,
            property=self.prop
        )
        if isinstance(rsp, dict):
            for key, value in rsp.items():
                print(f"{key}: {value}")
        else:
            print(rsp)
        return 0


class QOMList(QOMCommand):
    """
    QOM Command - List the properties at a given path.

    usage: qom-list [-h] [--socket SOCKET] <path>

    List QOM properties at a given path

    positional arguments:
      <path>                QOM path

    optional arguments:
      -h, --help            show this help message and exit
      --socket SOCKET, -s SOCKET
                            QMP socket path or address (addr:port). May also be
                            set via QMP_SOCKET environment variable.
    """
    name = 'list'
    help = 'List QOM properties at a given path'

    @classmethod
    def configure_parser(cls, parser: argparse.ArgumentParser) -> None:
        super().configure_parser(parser)
        parser.add_argument(
            'path',
            metavar='<path>',
            action='store',
            help='QOM path',
        )

    def __init__(self, args: argparse.Namespace):
        super().__init__(args)
        self.path = args.path

    def run(self) -> int:
        rsp = self.qom_list(self.path)
        for item in rsp:
            if item.child:
                print(f"{item.name}/")
            elif item.link:
                print(f"@{item.name}/")
            else:
                print(item.name)
        return 0


class QOMTree(QOMCommand):
    """
    QOM Command - Show the full tree below a given path.

    usage: qom-tree [-h] [--socket SOCKET] [<path>]

    Show QOM tree from a given path

    positional arguments:
      <path>                QOM path

    optional arguments:
      -h, --help            show this help message and exit
      --socket SOCKET, -s SOCKET
                            QMP socket path or address (addr:port). May also be
                            set via QMP_SOCKET environment variable.
    """
    name = 'tree'
    help = 'Show QOM tree from a given path'

    @classmethod
    def configure_parser(cls, parser: argparse.ArgumentParser) -> None:
        super().configure_parser(parser)
        parser.add_argument(
            'path',
            metavar='<path>',
            action='store',
            help='QOM path',
            nargs='?',
            default='/'
        )

    def __init__(self, args: argparse.Namespace):
        super().__init__(args)
        self.path = args.path

    def _list_node(self, path: str) -> None:
        print(path)
        items = self.qom_list(path)
        for item in items:
            if item.child:
                continue
            try:
                rsp = self.qmp.command('qom-get', path=path,
                                       property=item.name)
                print(f"  {item.name}: {rsp} ({item.type})")
            except QMPResponseError as err:
                print(f"  {item.name}: <EXCEPTION: {err!s}> ({item.type})")
        print('')
        for item in items:
            if not item.child:
                continue
            if path == '/':
                path = ''
            self._list_node(f"{path}/{item.name}")

    def run(self) -> int:
        self._list_node(self.path)
        return 0


def main() -> int:
    """QOM script main entry point."""
    parser = argparse.ArgumentParser(
        description='Query and manipulate QOM data'
    )
    subparsers = parser.add_subparsers(
        title='QOM commands',
        dest='command'
    )

    for command in QOMCommand.__subclasses__():
        command.register(subparsers)

    args = parser.parse_args()

    if args.command is None:
        parser.error('Command not specified.')
        return 1

    cmd_class = args.cmd_class
    assert isinstance(cmd_class, type(QOMCommand))
    return cmd_class.command_runner(args)
