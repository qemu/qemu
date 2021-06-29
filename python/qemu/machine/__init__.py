"""
QEMU development and testing library.

This library provides a few high-level classes for driving QEMU from a
test suite, not intended for production use.

 | QEMUQtestProtocol: send/receive qtest messages.
 | QEMUMachine: Configure and Boot a QEMU VM
 | +-- QEMUQtestMachine: VM class, with a qtest socket.

"""

# Copyright (C) 2020-2021 John Snow for Red Hat Inc.
# Copyright (C) 2015-2016 Red Hat Inc.
# Copyright (C) 2012 IBM Corp.
#
# Authors:
#  John Snow <jsnow@redhat.com>
#  Fam Zheng <fam@euphon.net>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#

# pylint: disable=import-error
# see: https://github.com/PyCQA/pylint/issues/3624
# see: https://github.com/PyCQA/pylint/issues/3651
from .machine import QEMUMachine
from .qtest import QEMUQtestMachine, QEMUQtestProtocol


__all__ = (
    'QEMUMachine',
    'QEMUQtestProtocol',
    'QEMUQtestMachine',
)
