# Test class and utilities for functional tests
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import logging
import os
import sys
import uuid
import tempfile

import avocado

SRC_ROOT_DIR = os.path.join(os.path.dirname(__file__), '..', '..', '..')
sys.path.append(os.path.join(SRC_ROOT_DIR, 'python'))

from qemu.machine import QEMUMachine

def is_readable_executable_file(path):
    return os.path.isfile(path) and os.access(path, os.R_OK | os.X_OK)


def pick_default_qemu_bin(arch=None):
    """
    Picks the path of a QEMU binary, starting either in the current working
    directory or in the source tree root directory.

    :param arch: the arch to use when looking for a QEMU binary (the target
                 will match the arch given).  If None (the default), arch
                 will be the current host system arch (as given by
                 :func:`os.uname`).
    :type arch: str
    :returns: the path to the default QEMU binary or None if one could not
              be found
    :rtype: str or None
    """
    if arch is None:
        arch = os.uname()[4]
    # qemu binary path does not match arch for powerpc, handle it
    if 'ppc64le' in arch:
        arch = 'ppc64'
    qemu_bin_relative_path = os.path.join("%s-softmmu" % arch,
                                          "qemu-system-%s" % arch)
    if is_readable_executable_file(qemu_bin_relative_path):
        return qemu_bin_relative_path

    qemu_bin_from_src_dir_path = os.path.join(SRC_ROOT_DIR,
                                              qemu_bin_relative_path)
    if is_readable_executable_file(qemu_bin_from_src_dir_path):
        return qemu_bin_from_src_dir_path


def wait_for_console_pattern(test, success_message, failure_message=None):
    """
    Waits for messages to appear on the console, while logging the content

    :param test: an Avocado test containing a VM that will have its console
                 read and probed for a success or failure message
    :type test: :class:`avocado_qemu.Test`
    :param success_message: if this message appears, test succeeds
    :param failure_message: if this message appears, test fails
    """
    console = test.vm.console_socket.makefile()
    console_logger = logging.getLogger('console')
    while True:
        msg = console.readline().strip()
        if not msg:
            continue
        console_logger.debug(msg)
        if success_message in msg:
            break
        if failure_message and failure_message in msg:
            console.close()
            fail = 'Failure message found in console: %s' % failure_message
            test.fail(fail)


def exec_command_and_wait_for_pattern(test, command,
                                      success_message, failure_message=None):
    """
    Send a command to a console (appending CRLF characters), then wait
    for success_message to appear on the console, while logging the.
    content. Mark the test as failed if failure_message is found instead.

    :param test: an Avocado test containing a VM that will have its console
                 read and probed for a success or failure message
    :type test: :class:`avocado_qemu.Test`
    :param command: the command to send
    :param success_message: if this message appears, test succeeds
    :param failure_message: if this message appears, test fails
    """
    command += '\r'
    test.vm.console_socket.sendall(command.encode())
    wait_for_console_pattern(test, success_message, failure_message)


class Test(avocado.Test):
    def setUp(self):
        self._vms = {}
        arches = self.tags.get('arch', [])
        if len(arches) == 1:
            arch = arches.pop()
        else:
            arch = None
        self.arch = self.params.get('arch', default=arch)
        default_qemu_bin = pick_default_qemu_bin(arch=self.arch)
        self.qemu_bin = self.params.get('qemu_bin',
                                        default=default_qemu_bin)
        if self.qemu_bin is None:
            self.cancel("No QEMU binary defined or found in the source tree")

    def _new_vm(self, *args):
        vm = QEMUMachine(self.qemu_bin, sock_dir=tempfile.mkdtemp())
        if args:
            vm.add_args(*args)
        return vm

    @property
    def vm(self):
        return self.get_vm(name='default')

    def get_vm(self, *args, name=None):
        if not name:
            name = str(uuid.uuid4())
        if self._vms.get(name) is None:
            self._vms[name] = self._new_vm(*args)
        return self._vms[name]

    def tearDown(self):
        for vm in self._vms.values():
            vm.shutdown()
