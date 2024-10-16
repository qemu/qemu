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
import subprocess
import sys
import tempfile
import time
import uuid

import avocado
from avocado.utils import ssh
from avocado.utils.path import find_command

from qemu.machine import QEMUMachine
from qemu.utils import (get_info_usernet_hostfwd_port, kvm_available,
                        tcg_available)


#: The QEMU build root directory.  It may also be the source directory
#: if building from the source dir, but it's safer to use BUILD_DIR for
#: that purpose.  Be aware that if this code is moved outside of a source
#: and build tree, it will not be accurate.
BUILD_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))


def has_cmd(name, args=None):
    """
    This function is for use in a @avocado.skipUnless decorator, e.g.:

        @skipUnless(*has_cmd('sudo -n', ('sudo', '-n', 'true')))
        def test_something_that_needs_sudo(self):
            ...
    """

    if args is None:
        args = ('which', name)

    try:
        _, stderr, exitcode = run_cmd(args)
    except Exception as e:
        exitcode = -1
        stderr = str(e)

    if exitcode != 0:
        cmd_line = ' '.join(args)
        err = f'{name} required, but "{cmd_line}" failed: {stderr.strip()}'
        return (False, err)
    else:
        return (True, '')

def has_cmds(*cmds):
    """
    This function is for use in a @avocado.skipUnless decorator and
    allows checking for the availability of multiple commands, e.g.:

        @skipUnless(*has_cmds(('cmd1', ('cmd1', '--some-parameter')),
                              'cmd2', 'cmd3'))
        def test_something_that_needs_cmd1_and_cmd2(self):
            ...
    """

    for cmd in cmds:
        if isinstance(cmd, str):
            cmd = (cmd,)

        ok, errstr = has_cmd(*cmd)
        if not ok:
            return (False, errstr)

    return (True, '')

def run_cmd(args):
    subp = subprocess.Popen(args,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            universal_newlines=True)
    stdout, stderr = subp.communicate()
    ret = subp.returncode

    return (stdout, stderr, ret)

def is_readable_executable_file(path):
    return os.path.isfile(path) and os.access(path, os.R_OK | os.X_OK)


def pick_default_qemu_bin(bin_prefix='qemu-system-', arch=None):
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
    qemu_bin_name = bin_prefix + arch
    qemu_bin_paths = [
        os.path.join(".", qemu_bin_name),
        os.path.join(BUILD_DIR, qemu_bin_name),
        os.path.join(BUILD_DIR, "build", qemu_bin_name),
    ]
    for path in qemu_bin_paths:
        if is_readable_executable_file(path):
            return path
    return None


def _console_interaction(test, success_message, failure_message,
                         send_string, keep_sending=False, vm=None):
    assert not keep_sending or send_string
    if vm is None:
        vm = test.vm
    console = vm.console_file
    console_logger = logging.getLogger('console')
    while True:
        if send_string:
            vm.console_socket.sendall(send_string.encode())
            if not keep_sending:
                send_string = None # send only once

        # Only consume console output if waiting for something
        if success_message is None and failure_message is None:
            if send_string is None:
                break
            continue

        try:
            msg = console.readline().decode().strip()
        except UnicodeDecodeError:
            msg = None
        if not msg:
            continue
        console_logger.debug(msg)
        if success_message is None or success_message in msg:
            break
        if failure_message and failure_message in msg:
            console.close()
            fail = 'Failure message found in console: "%s". Expected: "%s"' % \
                    (failure_message, success_message)
            test.fail(fail)

def interrupt_interactive_console_until_pattern(test, success_message,
                                                failure_message=None,
                                                interrupt_string='\r'):
    """
    Keep sending a string to interrupt a console prompt, while logging the
    console output. Typical use case is to break a boot loader prompt, such:

        Press a key within 5 seconds to interrupt boot process.
        5
        4
        3
        2
        1
        Booting default image...

    :param test: an Avocado test containing a VM that will have its console
                 read and probed for a success or failure message
    :type test: :class:`avocado_qemu.QemuSystemTest`
    :param success_message: if this message appears, test succeeds
    :param failure_message: if this message appears, test fails
    :param interrupt_string: a string to send to the console before trying
                             to read a new line
    """
    _console_interaction(test, success_message, failure_message,
                         interrupt_string, True)

def wait_for_console_pattern(test, success_message, failure_message=None,
                             vm=None):
    """
    Waits for messages to appear on the console, while logging the content

    :param test: an Avocado test containing a VM that will have its console
                 read and probed for a success or failure message
    :type test: :class:`avocado_qemu.QemuSystemTest`
    :param success_message: if this message appears, test succeeds
    :param failure_message: if this message appears, test fails
    """
    _console_interaction(test, success_message, failure_message, None, vm=vm)

def exec_command(test, command):
    """
    Send a command to a console (appending CRLF characters), while logging
    the content.

    :param test: an Avocado test containing a VM.
    :type test: :class:`avocado_qemu.QemuSystemTest`
    :param command: the command to send
    :type command: str
    """
    _console_interaction(test, None, None, command + '\r')

def exec_command_and_wait_for_pattern(test, command,
                                      success_message, failure_message=None):
    """
    Send a command to a console (appending CRLF characters), then wait
    for success_message to appear on the console, while logging the.
    content. Mark the test as failed if failure_message is found instead.

    :param test: an Avocado test containing a VM that will have its console
                 read and probed for a success or failure message
    :type test: :class:`avocado_qemu.QemuSystemTest`
    :param command: the command to send
    :param success_message: if this message appears, test succeeds
    :param failure_message: if this message appears, test fails
    """
    _console_interaction(test, success_message, failure_message, command + '\r')

class QemuBaseTest(avocado.Test):

    # default timeout for all tests, can be overridden
    timeout = 120

    def _get_unique_tag_val(self, tag_name):
        """
        Gets a tag value, if unique for a key
        """
        vals = self.tags.get(tag_name, [])
        if len(vals) == 1:
            return vals.pop()
        return None

    def setUp(self, bin_prefix):
        self.arch = self.params.get('arch',
                                    default=self._get_unique_tag_val('arch'))

        self.cpu = self.params.get('cpu',
                                   default=self._get_unique_tag_val('cpu'))

        default_qemu_bin = pick_default_qemu_bin(bin_prefix, arch=self.arch)
        self.qemu_bin = self.params.get('qemu_bin',
                                        default=default_qemu_bin)
        if self.qemu_bin is None:
            self.cancel("No QEMU binary defined or found in the build tree")

    def fetch_asset(self, name,
                    asset_hash, algorithm=None,
                    locations=None, expire=None,
                    find_only=False, cancel_on_missing=True):
        return super().fetch_asset(name,
                        asset_hash=asset_hash,
                        algorithm=algorithm,
                        locations=locations,
                        expire=expire,
                        find_only=find_only,
                        cancel_on_missing=cancel_on_missing)


class QemuSystemTest(QemuBaseTest):
    """Facilitates system emulation tests."""

    def setUp(self):
        self._vms = {}

        super().setUp('qemu-system-')

        accel_required = self._get_unique_tag_val('accel')
        if accel_required:
            self.require_accelerator(accel_required)

        self.machine = self.params.get('machine',
                                       default=self._get_unique_tag_val('machine'))

    def require_accelerator(self, accelerator):
        """
        Requires an accelerator to be available for the test to continue

        It takes into account the currently set qemu binary.

        If the check fails, the test is canceled.  If the check itself
        for the given accelerator is not available, the test is also
        canceled.

        :param accelerator: name of the accelerator, such as "kvm" or "tcg"
        :type accelerator: str
        """
        checker = {'tcg': tcg_available,
                   'kvm': kvm_available}.get(accelerator)
        if checker is None:
            self.cancel("Don't know how to check for the presence "
                        "of accelerator %s" % accelerator)
        if not checker(qemu_bin=self.qemu_bin):
            self.cancel("%s accelerator does not seem to be "
                        "available" % accelerator)

    def require_netdev(self, netdevname):
        netdevhelp = run_cmd([self.qemu_bin,
                             '-M', 'none', '-netdev', 'help'])[0];
        if netdevhelp.find('\n' + netdevname + '\n') < 0:
            self.cancel('no support for user networking')

    def _new_vm(self, name, *args):
        self._sd = tempfile.TemporaryDirectory(prefix="qemu_")
        vm = QEMUMachine(self.qemu_bin, base_temp_dir=self.workdir,
                         log_dir=self.logdir)
        self.log.debug('QEMUMachine "%s" created', name)
        self.log.debug('QEMUMachine "%s" temp_dir: %s', name, vm.temp_dir)
        self.log.debug('QEMUMachine "%s" log_dir: %s', name, vm.log_dir)
        if args:
            vm.add_args(*args)
        return vm

    def get_qemu_img(self):
        self.log.debug('Looking for and selecting a qemu-img binary')

        # If qemu-img has been built, use it, otherwise the system wide one
        # will be used.
        qemu_img = os.path.join(BUILD_DIR, 'qemu-img')
        if not os.path.exists(qemu_img):
            qemu_img = find_command('qemu-img', False)
        if qemu_img is False:
            self.cancel('Could not find "qemu-img"')

        return qemu_img

    @property
    def vm(self):
        return self.get_vm(name='default')

    def get_vm(self, *args, name=None):
        if not name:
            name = str(uuid.uuid4())
        if self._vms.get(name) is None:
            self._vms[name] = self._new_vm(name, *args)
            if self.cpu is not None:
                self._vms[name].add_args('-cpu', self.cpu)
            if self.machine is not None:
                self._vms[name].set_machine(self.machine)
        return self._vms[name]

    def set_vm_arg(self, arg, value):
        """
        Set an argument to list of extra arguments to be given to the QEMU
        binary. If the argument already exists then its value is replaced.

        :param arg: the QEMU argument, such as "-cpu" in "-cpu host"
        :type arg: str
        :param value: the argument value, such as "host" in "-cpu host"
        :type value: str
        """
        if not arg or not value:
            return
        if arg not in self.vm.args:
            self.vm.args.extend([arg, value])
        else:
            idx = self.vm.args.index(arg) + 1
            if idx < len(self.vm.args):
                self.vm.args[idx] = value
            else:
                self.vm.args.append(value)

    def tearDown(self):
        for vm in self._vms.values():
            vm.shutdown()
        self._sd = None
        super().tearDown()


class LinuxSSHMixIn:
    """Contains utility methods for interacting with a guest via SSH."""

    def ssh_connect(self, username, credential, credential_is_key=True):
        self.ssh_logger = logging.getLogger('ssh')
        res = self.vm.cmd('human-monitor-command',
                          command_line='info usernet')
        port = get_info_usernet_hostfwd_port(res)
        self.assertIsNotNone(port)
        self.assertGreater(port, 0)
        self.log.debug('sshd listening on port: %d', port)
        if credential_is_key:
            self.ssh_session = ssh.Session('127.0.0.1', port=port,
                                           user=username, key=credential)
        else:
            self.ssh_session = ssh.Session('127.0.0.1', port=port,
                                           user=username, password=credential)
        for i in range(10):
            try:
                self.ssh_session.connect()
                return
            except:
                time.sleep(i)
        self.fail('ssh connection timeout')

    def ssh_command(self, command):
        self.ssh_logger.info(command)
        result = self.ssh_session.cmd(command)
        stdout_lines = [line.rstrip() for line
                        in result.stdout_text.splitlines()]
        for line in stdout_lines:
            self.ssh_logger.info(line)
        stderr_lines = [line.rstrip() for line
                        in result.stderr_text.splitlines()]
        for line in stderr_lines:
            self.ssh_logger.warning(line)

        self.assertEqual(result.exit_status, 0,
                         f'Guest command failed: {command}')
        return stdout_lines, stderr_lines

    def ssh_command_output_contains(self, cmd, exp):
        stdout, _ = self.ssh_command(cmd)
        for line in stdout:
            if exp in line:
                break
        else:
            self.fail('"%s" output does not contain "%s"' % (cmd, exp))
