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
from pathlib import Path
import pycotap
import shutil
from subprocess import run
import sys
import tempfile
import unittest
import uuid

from qemu.machine import QEMUMachine
from qemu.utils import kvm_available, tcg_available

from .archive import archive_extract
from .asset import Asset
from .config import BUILD_DIR, dso_suffix
from .uncompress import uncompress


class QemuBaseTest(unittest.TestCase):

    '''
    @params compressed: filename, Asset, or file-like object to uncompress
    @params format: optional compression format (gzip, lzma)

    Uncompresses @compressed into the scratch directory.

    If @format is None, heuristics will be applied to guess the format
    from the filename or Asset URL. @format must be non-None if @uncompressed
    is a file-like object.

    Returns the fully qualified path to the uncompressed file
    '''
    def uncompress(self, compressed, format=None):
        self.log.debug(f"Uncompress {compressed} format={format}")
        if type(compressed) == Asset:
            compressed.fetch()

        (name, ext) = os.path.splitext(str(compressed))
        uncompressed = self.scratch_file(os.path.basename(name))

        uncompress(compressed, uncompressed, format)

        return uncompressed

    '''
    @params archive: filename, Asset, or file-like object to extract
    @params format: optional archive format (tar, zip, deb, cpio)
    @params sub_dir: optional sub-directory to extract into
    @params member: optional member file to limit extraction to

    Extracts @archive into the scratch directory, or a directory beneath
    named by @sub_dir. All files are extracted unless @member specifies
    a limit.

    If @format is None, heuristics will be applied to guess the format
    from the filename or Asset URL. @format must be non-None if @archive
    is a file-like object.

    If @member is non-None, returns the fully qualified path to @member
    '''
    def archive_extract(self, archive, format=None, sub_dir=None, member=None):
        self.log.debug(f"Extract {archive} format={format}" +
                       f"sub_dir={sub_dir} member={member}")
        if type(archive) == Asset:
            archive.fetch()
        if sub_dir is None:
            archive_extract(archive, self.scratch_file(), format, member)
        else:
            archive_extract(archive, self.scratch_file(sub_dir),
                            format, member)

        if member is not None:
            return self.scratch_file(member)
        return None

    '''
    Create a temporary directory suitable for storing UNIX
    socket paths.

    Returns: a tempfile.TemporaryDirectory instance
    '''
    def socket_dir(self):
        if self.socketdir is None:
            self.socketdir = tempfile.TemporaryDirectory(
                prefix="qemu_func_test_sock_")
        return self.socketdir

    '''
    @params args list of zero or more subdirectories or file

    Construct a path for accessing a data file located
    relative to the source directory that is the root for
    functional tests.

    @args may be an empty list to reference the root dir
    itself, may be a single element to reference a file in
    the root directory, or may be multiple elements to
    reference a file nested below. The path components
    will be joined using the platform appropriate path
    separator.

    Returns: string representing a file path
    '''
    def data_file(self, *args):
        return str(Path(Path(__file__).parent.parent, *args))

    '''
    @params args list of zero or more subdirectories or file

    Construct a path for accessing a data file located
    relative to the build directory root.

    @args may be an empty list to reference the build dir
    itself, may be a single element to reference a file in
    the build directory, or may be multiple elements to
    reference a file nested below. The path components
    will be joined using the platform appropriate path
    separator.

    Returns: string representing a file path
    '''
    def build_file(self, *args):
        return str(Path(BUILD_DIR, *args))

    '''
    @params args list of zero or more subdirectories or file

    Construct a path for accessing/creating a scratch file
    located relative to a temporary directory dedicated to
    this test case. The directory and its contents will be
    purged upon completion of the test.

    @args may be an empty list to reference the scratch dir
    itself, may be a single element to reference a file in
    the scratch directory, or may be multiple elements to
    reference a file nested below. The path components
    will be joined using the platform appropriate path
    separator.

    Returns: string representing a file path
    '''
    def scratch_file(self, *args):
        return str(Path(self.workdir, *args))

    '''
    @params args list of zero or more subdirectories or file

    Construct a path for accessing/creating a log file
    located relative to a temporary directory dedicated to
    this test case. The directory and its log files will be
    preserved upon completion of the test.

    @args may be an empty list to reference the log dir
    itself, may be a single element to reference a file in
    the log directory, or may be multiple elements to
    reference a file nested below. The path components
    will be joined using the platform appropriate path
    separator.

    Returns: string representing a file path
    '''
    def log_file(self, *args):
        return str(Path(self.outputdir, *args))

    '''
    @params plugin name

    Return the full path to the plugin taking into account any host OS
    specific suffixes.
    '''
    def plugin_file(self, plugin_name):
        sfx = dso_suffix()
        return os.path.join('tests', 'tcg', 'plugins', f'{plugin_name}.{sfx}')

    def assets_available(self):
        for name, asset in vars(self.__class__).items():
            if name.startswith("ASSET_") and type(asset) == Asset:
                if not asset.available():
                    self.log.debug(f"Asset {asset.url} not available")
                    return False
        return True

    def setUp(self):
        self.qemu_bin = os.getenv('QEMU_TEST_QEMU_BINARY')
        self.assertIsNotNone(self.qemu_bin, 'QEMU_TEST_QEMU_BINARY must be set')
        self.arch = self.qemu_bin.split('-')[-1]
        self.socketdir = None

        self.outputdir = self.build_file('tests', 'functional',
                                         self.arch, self.id())
        self.workdir = os.path.join(self.outputdir, 'scratch')
        os.makedirs(self.workdir, exist_ok=True)

        self.log_filename = self.log_file('base.log')
        self.log = logging.getLogger('qemu-test')
        self.log.setLevel(logging.DEBUG)
        self._log_fh = logging.FileHandler(self.log_filename, mode='w')
        self._log_fh.setLevel(logging.DEBUG)
        fileFormatter = logging.Formatter(
            '%(asctime)s - %(levelname)s: %(message)s')
        self._log_fh.setFormatter(fileFormatter)
        self.log.addHandler(self._log_fh)

        # Capture QEMUMachine logging
        self.machinelog = logging.getLogger('qemu.machine')
        self.machinelog.setLevel(logging.DEBUG)
        self.machinelog.addHandler(self._log_fh)

        if not self.assets_available():
            self.skipTest('One or more assets is not available')

    def tearDown(self):
        if "QEMU_TEST_KEEP_SCRATCH" not in os.environ:
            shutil.rmtree(self.workdir)
        if self.socketdir is not None:
            shutil.rmtree(self.socketdir.name)
            self.socketdir = None
        self.machinelog.removeHandler(self._log_fh)
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

            if hasattr(test, "log_filename"):
                print('More information on ' + test.id() + ' could be found here:'
                      '\n %s' % test.log_filename, file=sys.stderr)
                if hasattr(test, 'console_log_name'):
                    print(' %s' % test.console_log_name, file=sys.stderr)
        sys.exit(not res.result.wasSuccessful())


class QemuUserTest(QemuBaseTest):

    def setUp(self):
        super().setUp()
        self._ldpath = []

    def add_ldpath(self, ldpath):
        self._ldpath.append(os.path.abspath(ldpath))

    def run_cmd(self, bin_path, args=[]):
        return run([self.qemu_bin]
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

        super().setUp()

        console_log = logging.getLogger('console')
        console_log.setLevel(logging.DEBUG)
        self.console_log_name = self.log_file('console.log')
        self._console_log_fh = logging.FileHandler(self.console_log_name,
                                                   mode='w')
        self._console_log_fh.setLevel(logging.DEBUG)
        fileFormatter = logging.Formatter('%(asctime)s: %(message)s')
        self._console_log_fh.setFormatter(fileFormatter)
        console_log.addHandler(self._console_log_fh)

    def set_machine(self, machinename):
        # TODO: We should use QMP to get the list of available machines
        if not self._machinehelp:
            self._machinehelp = run(
                [self.qemu_bin, '-M', 'help'],
                capture_output=True, check=True, encoding='utf8').stdout
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
        help = run([self.qemu_bin,
                    '-M', 'none', '-netdev', 'help'],
                   capture_output=True, check=True, encoding='utf8').stdout;
        if help.find('\n' + netdevname + '\n') < 0:
            self.skipTest('no support for " + netdevname + " networking')

    def require_device(self, devicename):
        help = run([self.qemu_bin,
                    '-M', 'none', '-device', 'help'],
                   capture_output=True, check=True, encoding='utf8').stdout;
        if help.find(devicename) < 0:
            self.skipTest('no support for device ' + devicename)

    def _new_vm(self, name, *args):
        vm = QEMUMachine(self.qemu_bin,
                         name=name,
                         base_temp_dir=self.workdir,
                         log_dir=self.log_file())
        self.log.debug('QEMUMachine "%s" created', name)
        self.log.debug('QEMUMachine "%s" temp_dir: %s', name, vm.temp_dir)

        sockpath = os.environ.get("QEMU_TEST_QMP_BACKDOOR", None)
        if sockpath is not None:
            vm.add_args("-chardev",
                        f"socket,id=backdoor,path={sockpath},server=on,wait=off",
                        "-mon", "chardev=backdoor,mode=control")

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
