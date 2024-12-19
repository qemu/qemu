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


def which(tool):
    """ looks up the full path for @tool, returns None if not found
        or if @tool does not have executable permissions.
    """
    paths=os.getenv('PATH')
    for p in paths.split(os.path.pathsep):
        p = os.path.join(p, tool)
        if os.access(p, os.X_OK):
            return p
    return None

def is_readable_executable_file(path):
    return os.path.isfile(path) and os.access(path, os.R_OK | os.X_OK)

# @test: functional test to fail if @failure is seen
# @vm: the VM whose console to process
# @success: a non-None string to look for
# @failure: a string to look for that triggers test failure, or None
#
# Read up to 1 line of text from @vm, looking for @success
# and optionally @failure.
#
# If @success or @failure are seen, immediately return True,
# even if end of line is not yet seen. ie remainder of the
# line is left unread.
#
# If end of line is seen, with neither @success or @failure
# return False
#
# If @failure is seen, then mark @test as failed
def _console_read_line_until_match(test, vm, success, failure):
    msg = bytes([])
    done = False
    while True:
        c = vm.console_socket.recv(1)
        if c is None:
            done = True
            test.fail(
                f"EOF in console, expected '{success}'")
            break
        msg += c

        if success in msg:
            done = True
            break
        if failure and failure in msg:
            done = True
            vm.console_socket.close()
            test.fail(
                f"'{failure}' found in console, expected '{success}'")

        if c == b'\n':
            break

    console_logger = logging.getLogger('console')
    try:
        console_logger.debug(msg.decode().strip())
    except:
        console_logger.debug(msg)

    return done

def _console_interaction(test, success_message, failure_message,
                         send_string, keep_sending=False, vm=None):
    assert not keep_sending or send_string
    assert success_message or send_string

    if vm is None:
        vm = test.vm

    test.log.debug(
        f"Console interaction: success_msg='{success_message}' " +
        f"failure_msg='{failure_message}' send_string='{send_string}'")

    # We'll process console in bytes, to avoid having to
    # deal with unicode decode errors from receiving
    # partial utf8 byte sequences
    success_message_b = None
    if success_message is not None:
        success_message_b = success_message.encode()

    failure_message_b = None
    if failure_message is not None:
        failure_message_b = failure_message.encode()

    while True:
        if send_string:
            vm.console_socket.sendall(send_string.encode())
            if not keep_sending:
                send_string = None # send only once

        # Only consume console output if waiting for something
        if success_message is None:
            if send_string is None:
                break
            continue

        if _console_read_line_until_match(test, vm,
                                          success_message_b,
                                          failure_message_b):
            break

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
    assert success_message
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
    assert success_message
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
    assert success_message
    _console_interaction(test, success_message, failure_message, command + '\r')

def get_qemu_img(test):
    test.log.debug('Looking for and selecting a qemu-img binary')

    # If qemu-img has been built, use it, otherwise the system wide one
    # will be used.
    qemu_img = test.build_file('qemu-img')
    if os.path.exists(qemu_img):
        return qemu_img
    qemu_img = which('qemu-img')
    if qemu_img is not None:
        return qemu_img
    test.skipTest(f"qemu-img not found in build dir or '$PATH'")
