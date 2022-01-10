"""
QEMU Monitor Protocol (QMP) development library & tooling.

This package provides a fairly low-level class for communicating
asynchronously with QMP protocol servers, as implemented by QEMU, the
QEMU Guest Agent, and the QEMU Storage Daemon.

`QMPClient` provides the main functionality of this package. All errors
raised by this library derive from `QMPError`, see `aqmp.error` for
additional detail. See `aqmp.events` for an in-depth tutorial on
managing QMP events.
"""

# Copyright (C) 2020, 2021 John Snow for Red Hat, Inc.
#
# Authors:
#  John Snow <jsnow@redhat.com>
#
# Based on earlier work by Luiz Capitulino <lcapitulino@redhat.com>.
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.

import logging

from .error import QMPError
from .events import EventListener
from .message import Message
from .protocol import (
    ConnectError,
    Runstate,
    SocketAddrT,
    StateError,
)
from .qmp_client import ExecInterruptedError, ExecuteError, QMPClient


# Suppress logging unless an application engages it.
logging.getLogger('qemu.aqmp').addHandler(logging.NullHandler())


# The order of these fields impact the Sphinx documentation order.
__all__ = (
    # Classes, most to least important
    'QMPClient',
    'Message',
    'EventListener',
    'Runstate',

    # Exceptions, most generic to most explicit
    'QMPError',
    'StateError',
    'ConnectError',
    'ExecuteError',
    'ExecInterruptedError',

    # Type aliases
    'SocketAddrT',
)
