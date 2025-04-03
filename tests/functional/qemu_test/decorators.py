# SPDX-License-Identifier: GPL-2.0-or-later
#
# Decorators useful in functional tests

import importlib
import os
import platform
from unittest import skipIf, skipUnless

from .cmd import which

'''
Decorator to skip execution of a test if the list
of command binaries is not available in $PATH.
Example:

  @skipIfMissingCommands("mkisofs", "losetup")
'''
def skipIfMissingCommands(*args):
    has_cmds = True
    for cmd in args:
         if not which(cmd):
             has_cmds = False
             break

    return skipUnless(has_cmds, 'required command(s) "%s" not installed' %
                                ", ".join(args))

'''
Decorator to skip execution of a test if the current
host operating system does match one of the prohibited
ones.
Example

  @skipIfOperatingSystem("Linux", "Darwin")
'''
def skipIfOperatingSystem(*args):
    return skipIf(platform.system() in args,
                  'running on an OS (%s) that is not able to run this test' %
                  ", ".join(args))

'''
Decorator to skip execution of a test if the current
host machine does not match one of the permitted
machines.
Example

  @skipIfNotMachine("x86_64", "aarch64")
'''
def skipIfNotMachine(*args):
    return skipUnless(platform.machine() in args,
                      'not running on one of the required machine(s) "%s"' %
                      ", ".join(args))

'''
Decorator to skip execution of flaky tests, unless
the $QEMU_TEST_FLAKY_TESTS environment variable is set.
A bug URL must be provided that documents the observed
failure behaviour, so it can be tracked & re-evaluated
in future.

Historical tests may be providing "None" as the bug_url
but this should not be done for new test.

Example:

  @skipFlakyTest("https://gitlab.com/qemu-project/qemu/-/issues/NNN")
'''
def skipFlakyTest(bug_url):
    if bug_url is None:
        bug_url = "FIXME: reproduce flaky test and file bug report or remove"
    return skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'),
                      f'Test is unstable: {bug_url}')

'''
Decorator to skip execution of tests which are likely
to execute untrusted commands on the host, or commands
which process untrusted code, unless the
$QEMU_TEST_ALLOW_UNTRUSTED_CODE env var is set.
Example:

  @skipUntrustedTest()
'''
def skipUntrustedTest():
    return skipUnless(os.getenv('QEMU_TEST_ALLOW_UNTRUSTED_CODE'),
                      'Test runs untrusted code / processes untrusted data')

'''
Decorator to skip execution of tests which need large
data storage (over around 500MB-1GB mark) on the host,
unless the $QEMU_TEST_ALLOW_LARGE_STORAGE environment
variable is set

Example:

  @skipBigDataTest()
'''
def skipBigDataTest():
    return skipUnless(os.getenv('QEMU_TEST_ALLOW_LARGE_STORAGE'),
                      'Test requires large host storage space')

'''
Decorator to skip execution of tests which have a really long
runtime (and might e.g. time out if QEMU has been compiled with
debugging enabled) unless the $QEMU_TEST_ALLOW_SLOW
environment variable is set

Example:

  @skipSlowTest()
'''
def skipSlowTest():
    return skipUnless(os.getenv('QEMU_TEST_ALLOW_SLOW'),
                      'Test has a very long runtime and might time out')

'''
Decorator to skip execution of a test if the list
of python imports is not available.
Example:

  @skipIfMissingImports("numpy", "cv2")
'''
def skipIfMissingImports(*args):
    has_imports = True
    for impname in args:
        try:
            importlib.import_module(impname)
        except ImportError:
            has_imports = False
            break

    return skipUnless(has_imports, 'required import(s) "%s" not installed' %
                                   ", ".join(args))
