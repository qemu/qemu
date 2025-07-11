"""
QOM Command abstractions.
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
import os
import sys
from typing import (
    Any,
    Dict,
    List,
    Optional,
    Type,
    TypeVar,
)

from qemu.qmp import QMPError
from qemu.qmp.legacy import QEMUMonitorProtocol


class ObjectPropertyInfo:
    """
    Represents the return type from e.g. qom-list.
    """
    def __init__(self, name: str, type_: str,
                 description: Optional[str] = None,
                 default_value: Optional[object] = None):
        self.name = name
        self.type = type_
        self.description = description
        self.default_value = default_value

    @classmethod
    def make(cls, value: Dict[str, Any]) -> 'ObjectPropertyInfo':
        """
        Build an ObjectPropertyInfo from a Dict with an unknown shape.
        """
        assert value.keys() >= {'name', 'type'}
        assert value.keys() <= {'name', 'type', 'description', 'default-value'}
        return cls(value['name'], value['type'],
                   value.get('description'),
                   value.get('default-value'))

    @property
    def child(self) -> bool:
        """Is this property a child property?"""
        return self.type.startswith('child<')

    @property
    def link(self) -> bool:
        """Is this property a link property?"""
        return self.type.startswith('link<')


class ObjectPropertyValue:
    """
    Represents a property return from e.g. qom-tree-get
    """
    def __init__(self, name: str, type_: str, value: object):
        self.name = name
        self.type = type_
        self.value = value

    @classmethod
    def make(cls, value: Dict[str, Any]) -> 'ObjectPropertyValue':
        """
        Build an ObjectPropertyValue from a Dict with an unknown shape.
        """
        assert value.keys() >= {'name', 'type'}
        assert value.keys() <= {'name', 'type', 'value'}
        return cls(value['name'], value['type'], value.get('value'))

    @property
    def child(self) -> bool:
        """Is this property a child property?"""
        return self.type.startswith('child<')


class ObjectPropertiesValues:
    """
    Represents the return type from e.g. qom-list-get
    """
    # pylint: disable=too-few-public-methods

    def __init__(self, properties: List[ObjectPropertyValue]) -> None:
        self.properties = properties

    @classmethod
    def make(cls, value: Dict[str, Any]) -> 'ObjectPropertiesValues':
        """
        Build an ObjectPropertiesValues from a Dict with an unknown shape.
        """
        assert value.keys() == {'properties'}
        props = [ObjectPropertyValue(item['name'],
                                     item['type'],
                                     item.get('value'))
                 for item in value['properties']]
        return cls(props)


CommandT = TypeVar('CommandT', bound='QOMCommand')


class QOMCommand:
    """
    Represents a QOM sub-command.

    :param args: Parsed arguments, as returned from parser.parse_args.
    """
    name: str
    help: str

    def __init__(self, args: argparse.Namespace):
        if args.socket is None:
            raise QMPError("No QMP socket path or address given")
        self.qmp = QEMUMonitorProtocol(
            QEMUMonitorProtocol.parse_address(args.socket)
        )
        self.qmp.connect()

    @classmethod
    def register(cls, subparsers: Any) -> None:
        """
        Register this command with the argument parser.

        :param subparsers: argparse subparsers object, from "add_subparsers".
        """
        subparser = subparsers.add_parser(cls.name, help=cls.help,
                                          description=cls.help)
        cls.configure_parser(subparser)

    @classmethod
    def configure_parser(cls, parser: argparse.ArgumentParser) -> None:
        """
        Configure a parser with this command's arguments.

        :param parser: argparse parser or subparser object.
        """
        default_path = os.environ.get('QMP_SOCKET')
        parser.add_argument(
            '--socket', '-s',
            dest='socket',
            action='store',
            help='QMP socket path or address (addr:port).'
            ' May also be set via QMP_SOCKET environment variable.',
            default=default_path
        )
        parser.set_defaults(cmd_class=cls)

    @classmethod
    def add_path_prop_arg(cls, parser: argparse.ArgumentParser) -> None:
        """
        Add the <path>.<proptery> positional argument to this command.

        :param parser: The parser to add the argument to.
        """
        parser.add_argument(
            'path_prop',
            metavar='<path>.<property>',
            action='store',
            help="QOM path and property, separated by a period '.'"
        )

    def run(self) -> int:
        """
        Run this command.

        :return: 0 on success, 1 otherwise.
        """
        raise NotImplementedError

    def qom_list(self, path: str) -> List[ObjectPropertyInfo]:
        """
        :return: a strongly typed list from the 'qom-list' command.
        """
        rsp = self.qmp.cmd('qom-list', path=path)
        # qom-list returns List[ObjectPropertyInfo]
        assert isinstance(rsp, list)
        return [ObjectPropertyInfo.make(x) for x in rsp]

    def qom_list_get(self, paths: List[str]) -> List[ObjectPropertiesValues]:
        """
        :return: a strongly typed list from the 'qom-list-get' command.
        """
        rsp = self.qmp.cmd('qom-list-get', paths=paths)
        # qom-list-get returns List[ObjectPropertiesValues]
        assert isinstance(rsp, list)
        return [ObjectPropertiesValues.make(x) for x in rsp]

    @classmethod
    def command_runner(
            cls: Type[CommandT],
            args: argparse.Namespace
    ) -> int:
        """
        Run a fully-parsed subcommand, with error-handling for the CLI.

        :return: The return code from `run()`.
        """
        try:
            cmd = cls(args)
            return cmd.run()
        except QMPError as err:
            print(f"{type(err).__name__}: {err!s}", file=sys.stderr)
            return -1

    @classmethod
    def entry_point(cls) -> int:
        """
        Build this command's parser, parse arguments, and run the command.

        :return: `run`'s return code.
        """
        parser = argparse.ArgumentParser(description=cls.help)
        cls.configure_parser(parser)
        args = parser.parse_args()
        return cls.command_runner(args)
