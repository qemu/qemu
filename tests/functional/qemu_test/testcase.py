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
import subprocess
import pycotap
import sys
import unittest
import uuid

from qemu.machine import QEMUMachine
from qemu.utils import kvm_available, tcg_available

from .asset import Asset
from .cmd import run_cmd
from .config import BUILD_DIR


class QemuBaseTest(unittest.TestCase):

    qemu_bin = os.getenv('QEMU_TEST_QEMU_BINARY')
    arch = None

    workdir = None
    log = None
    logdir = None

    def setUp(self, bin_prefix):
        self.assertIsNotNone(self.qemu_bin, 'QEMU_TEST_QEMU_BINARY must be set')
        self.arch = self.qemu_bin.split('-')[-1]

        self.workdir = os.path.join(BUILD_DIR, 'tests/functional', self.arch,
                                    self.id())
        os.makedirs(self.workdir, exist_ok=True)

        self.logdir = self.workdir
        self.log_filename = os.path.join(self.logdir, 'base.log')
        self.log = logging.getLogger('qemu-test')
        self.log.setLevel(logging.DEBUG)
        self._log_fh = logging.FileHandler(self.log_filename, mode='w')
        self._log_fh.setLevel(logging.DEBUG)
        fileFormatter = logging.Formatter(
            '%(asctime)s - %(levelname)s: %(message)s')
        self._log_fh.setFormatter(fileFormatter)
        self.log.addHandler(self._log_fh)

    def tearDown(self):
        self.log.removeHandler(self._log_fh)

    def main():
        path = os.path.basename(sys.argv[0])[:-3]

        cache = os.environ.get("QEMU_TEST_PRECACHE", None)
        if cache is not None:
            Asset.precache_suites(path, cache)
            return

        tr = pycotap.TAPTestRunner(message_log = pycotap.LogMode.LogToError,
                                   test_output_log = pycotap.LogMode.LogToError)
        res = unittest.main(module = None, testRunner = tr, exit = False,
                            argv=["__dummy__", path])
        for (test, message) in res.result.errors + res.result.failures:
            print('More information on ' + test.id() + ' could be found here:'
                  '\n %s' % test.log_filename, file=sys.stderr)
            if hasattr(test, 'console_log_name'):
                print(' %s' % test.console_log_name, file=sys.stderr)
        sys.exit(not res.result.wasSuccessful())


class QemuUserTest(QemuBaseTest):

    def setUp(self):
        super().setUp('qemu-')
        self._ldpath = []

    def add_ldpath(self, ldpath):
        self._ldpath.append(os.path.abspath(ldpath))

    def run_cmd(self, bin_path, args=[]):
        return subprocess.run([self.qemu_bin]
                              + ["-L %s" % ldpath for ldpath in self._ldpath]
                              + [bin_path]
                              + args,
                              text=True, capture_output=True)

class QemuSystemTest(QemuBaseTest):
    """Facilitates system emulation tests."""

    cpu = None
    machine = None
    _machinehelp = None

    def setUp(self):
        self._vms = {}

        super().setUp('qemu-system-')

        console_log = logging.getLogger('console')
        console_log.setLevel(logging.DEBUG)
        self.console_log_name = os.path.join(self.workdir, 'console.log')
        self._console_log_fh = logging.FileHandler(self.console_log_name,
                                                   mode='w')
        self._console_log_fh.setLevel(logging.DEBUG)
        fileFormatter = logging.Formatter('%(asctime)s: %(message)s')
        self._console_log_fh.setFormatter(fileFormatter)
        console_log.addHandler(self._console_log_fh)

    def set_machine(self, machinename):
        # TODO: We should use QMP to get the list of available machines
        if not self._machinehelp:
            self._machinehelp = run_cmd([self.qemu_bin, '-M', 'help'])[0];
        if self._machinehelp.find(machinename) < 0:
            self.skipTest('no support for machine ' + machinename)
        self.machine = machinename

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
            self.skipTest("Don't know how to check for the presence "
                          "of accelerator %s" % accelerator)
        if not checker(qemu_bin=self.qemu_bin):
            self.skipTest("%s accelerator does not seem to be "
                          "available" % accelerator)

    def require_netdev(self, netdevname):
        netdevhelp = run_cmd([self.qemu_bin,
                             '-M', 'none', '-netdev', 'help'])[0];
        if netdevhelp.find('\n' + netdevname + '\n') < 0:
            self.skipTest('no support for " + netdevname + " networking')

    def require_device(self, devicename):
        devhelp = run_cmd([self.qemu_bin,
                           '-M', 'none', '-device', 'help'])[0];
        if devhelp.find(devicename) < 0:
            self.skipTest('no support for device ' + devicename)

    def _new_vm(self, name, *args):
        vm = QEMUMachine(self.qemu_bin, base_temp_dir=self.workdir)
        self.log.debug('QEMUMachine "%s" created', name)
        self.log.debug('QEMUMachine "%s" temp_dir: %s', name, vm.temp_dir)
        self.log.debug('QEMUMachine "%s" log_dir: %s', name, vm.log_dir)
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
        logging.getLogger('console').removeHandler(self._console_log_fh)
        super().tearDown()
