# Test class and utilities for functional tests
#
# Copyright 2018, 2024 Red Hat, Inc.
#
# Original Author (Avocado-based tests):
#  Cleber Rosa <crosa@redhat.com>
#
# Adaption for standalone version:
#  Thomas Huth <thuth@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import logging
import os
import os.path
import subprocess

from .config import BUILD_DIR


def has_cmd(name, args=None):
    """
    This function is for use in a @skipUnless decorator, e.g.:

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
    This function is for use in a @skipUnless decorator and
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

    :param test: a  test containing a VM that will have its console
                 read and probed for a success or failure message
    :type test: :class:`qemu_test.QemuSystemTest`
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

    :param test: a test containing a VM that will have its console
                 read and probed for a success or failure message
    :type test: :class:`qemu_test.QemuSystemTest`
    :param success_message: if this message appears, test succeeds
    :param failure_message: if this message appears, test fails
    """
    _console_interaction(test, success_message, failure_message, None, vm=vm)

def exec_command(test, command):
    """
    Send a command to a console (appending CRLF characters), while logging
    the content.

    :param test: a test containing a VM.
    :type test: :class:`qemu_test.QemuSystemTest`
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

    :param test: a test containing a VM that will have its console
                 read and probed for a success or failure message
    :type test: :class:`qemu_test.QemuSystemTest`
    :param command: the command to send
    :param success_message: if this message appears, test succeeds
    :param failure_message: if this message appears, test fails
    """
    _console_interaction(test, success_message, failure_message, command + '\r')

def get_qemu_img(test):
    test.log.debug('Looking for and selecting a qemu-img binary')

    # If qemu-img has been built, use it, otherwise the system wide one
    # will be used.
    qemu_img = os.path.join(BUILD_DIR, 'qemu-img')
    if os.path.exists(qemu_img):
        return qemu_img
    (has_system_qemu_img, errmsg) = has_cmd('qemu-img')
    if has_system_qemu_img:
        return 'qemu-img'
    test.skipTest(errmsg)
