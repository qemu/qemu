#!/usr/bin/env python3
#
# Exercise QEMU generated ACPI/SMBIOS tables using biosbits,
# https://biosbits.org/
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#
# Author:
#  Ani Sinha <anisinha@redhat.com>

# pylint: disable=invalid-name
# pylint: disable=consider-using-f-string

"""
This is QEMU ACPI/SMBIOS functional tests using biosbits.
Biosbits is available originally at https://biosbits.org/.
This test uses a fork of the upstream bits and has numerous fixes
including an upgraded acpica. The fork is located here:
https://gitlab.com/qemu-project/biosbits-bits .
"""

import os
import re
import shutil
import subprocess

from typing import (
    List,
    Optional,
    Sequence,
)
from qemu.machine import QEMUMachine
from qemu_test import (QemuSystemTest, Asset, skipIfMissingCommands,
                       skipIfNotMachine)


# default timeout of 120 secs is sometimes not enough for bits test.
BITS_TIMEOUT = 200

class QEMUBitsMachine(QEMUMachine): # pylint: disable=too-few-public-methods
    """
    A QEMU VM, with isa-debugcon enabled and bits iso passed
    using -cdrom to QEMU commandline.

    """
    def __init__(self,
                 binary: str,
                 args: Sequence[str] = (),
                 wrapper: Sequence[str] = (),
                 name: Optional[str] = None,
                 base_temp_dir: str = "/var/tmp",
                 debugcon_log: str = "debugcon-log.txt",
                 debugcon_addr: str = "0x403",
                 qmp_timer: Optional[float] = None):
        # pylint: disable=too-many-arguments

        if name is None:
            name = "qemu-bits-%d" % os.getpid()
        super().__init__(binary, args, wrapper=wrapper, name=name,
                         base_temp_dir=base_temp_dir,
                         qmp_timer=qmp_timer)
        self.debugcon_log = debugcon_log
        self.debugcon_addr = debugcon_addr
        self.base_temp_dir = base_temp_dir

    @property
    def _base_args(self) -> List[str]:
        args = super()._base_args
        args.extend([
            '-chardev',
            'file,path=%s,id=debugcon' %os.path.join(self.base_temp_dir,
                                                     self.debugcon_log),
            '-device',
            'isa-debugcon,iobase=%s,chardev=debugcon' %self.debugcon_addr,
        ])
        return args

    def base_args(self):
        """return the base argument to QEMU binary"""
        return self._base_args

@skipIfMissingCommands("xorriso", "mformat")
@skipIfNotMachine("x86_64")
class AcpiBitsTest(QemuSystemTest): #pylint: disable=too-many-instance-attributes
    """
    ACPI and SMBIOS tests using biosbits.
    """
    # in slower systems the test can take as long as 3 minutes to complete.
    timeout = BITS_TIMEOUT

    # following are some standard configuration constants
    # gitlab CI does shallow clones of depth 20
    BITS_INTERNAL_VER = 2020
    # commit hash must match the artifact tag below
    BITS_COMMIT_HASH = 'c7920d2b'
    # this is the latest bits release as of today.
    BITS_TAG = "qemu-bits-10262023"

    ASSET_BITS = Asset(("https://gitlab.com/qemu-project/"
                        "biosbits-bits/-/jobs/artifacts/%s/"
                        "download?job=qemu-bits-build" % BITS_TAG),
                       '1b8dd612c6831a6b491716a77acc486666aaa867051cdc34f7ce169c2e25f487')

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._vm = None

        self._debugcon_addr = '0x403'
        self._debugcon_log = 'debugcon-log.txt'

    def _print_log(self, log):
        self.logger.info('\nlogs from biosbits follows:')
        self.logger.info('==========================================\n')
        self.logger.info(log)
        self.logger.info('==========================================\n')

    def copy_bits_config(self):
        """ copies the bios bits config file into bits.
        """
        bits_config_file = self.data_file('acpi-bits',
                                          'bits-config',
                                          'bits-cfg.txt')
        target_config_dir = self.scratch_file('bits-%d' %
                                              self.BITS_INTERNAL_VER,
                                              'boot')
        self.assertTrue(os.path.exists(bits_config_file))
        self.assertTrue(os.path.exists(target_config_dir))
        shutil.copy2(bits_config_file, target_config_dir)
        self.logger.info('copied config file %s to %s',
                         bits_config_file, target_config_dir)

    def copy_test_scripts(self):
        """copies the python test scripts into bits. """

        bits_test_dir = self.data_file('acpi-bits', 'bits-tests')
        target_test_dir = self.scratch_file('bits-%d' % self.BITS_INTERNAL_VER,
                                            'boot', 'python')

        self.assertTrue(os.path.exists(bits_test_dir))
        self.assertTrue(os.path.exists(target_test_dir))

        for filename in os.listdir(bits_test_dir):
            if os.path.isfile(os.path.join(bits_test_dir, filename)) and \
               filename.endswith('.py2'):
                # All test scripts are named with extension .py2 so that
                # they are not run by accident.
                #
                # These scripts are intended to run inside the test VM
                # and are written for python 2.7 not python 3, hence
                # would cause syntax errors if loaded ouside the VM.
                newfilename = os.path.splitext(filename)[0] + '.py'
                shutil.copy2(os.path.join(bits_test_dir, filename),
                             os.path.join(target_test_dir, newfilename))
                self.logger.info('copied test file %s to %s',
                                 filename, target_test_dir)

                # now remove the pyc test file if it exists, otherwise the
                # changes in the python test script won't be executed.
                testfile_pyc = os.path.splitext(filename)[0] + '.pyc'
                if os.access(os.path.join(target_test_dir, testfile_pyc),
                             os.F_OK):
                    os.remove(os.path.join(target_test_dir, testfile_pyc))
                    self.logger.info('removed compiled file %s',
                                     os.path.join(target_test_dir,
                                     testfile_pyc))

    def fix_mkrescue(self, mkrescue):
        """ grub-mkrescue is a bash script with two variables, 'prefix' and
            'libdir'. They must be pointed to the right location so that the
            iso can be generated appropriately. We point the two variables to
            the directory where we have extracted our pre-built bits grub
            tarball.
        """
        grub_x86_64_mods = self.scratch_file('grub-inst-x86_64-efi')
        grub_i386_mods = self.scratch_file('grub-inst')

        self.assertTrue(os.path.exists(grub_x86_64_mods))
        self.assertTrue(os.path.exists(grub_i386_mods))

        new_script = ""
        with open(mkrescue, 'r', encoding='utf-8') as filehandle:
            orig_script = filehandle.read()
            new_script = re.sub('(^prefix=)(.*)',
                                r'\1"%s"' %grub_x86_64_mods,
                                orig_script, flags=re.M)
            new_script = re.sub('(^libdir=)(.*)', r'\1"%s/lib"' %grub_i386_mods,
                                new_script, flags=re.M)

        with open(mkrescue, 'w', encoding='utf-8') as filehandle:
            filehandle.write(new_script)

    def generate_bits_iso(self):
        """ Uses grub-mkrescue to generate a fresh bits iso with the python
            test scripts
        """
        bits_dir = self.scratch_file('bits-%d' % self.BITS_INTERNAL_VER)
        iso_file = self.scratch_file('bits-%d.iso' % self.BITS_INTERNAL_VER)
        mkrescue_script = self.scratch_file('grub-inst-x86_64-efi',
                                            'bin',
                                            'grub-mkrescue')

        self.assertTrue(os.access(mkrescue_script,
                                  os.R_OK | os.W_OK | os.X_OK))

        self.fix_mkrescue(mkrescue_script)

        self.logger.info('using grub-mkrescue for generating biosbits iso ...')

        try:
            if os.getenv('V') or os.getenv('BITS_DEBUG'):
                proc = subprocess.run([mkrescue_script, '-o', iso_file,
                                       bits_dir],
                                      stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT,
                                      check=True)
                self.logger.info("grub-mkrescue output %s" % proc.stdout)
            else:
                subprocess.check_call([mkrescue_script, '-o',
                                      iso_file, bits_dir],
                                      stderr=subprocess.DEVNULL,
                                      stdout=subprocess.DEVNULL)
        except Exception as e: # pylint: disable=broad-except
            self.skipTest("Error while generating the bits iso. "
                          "Pass V=1 in the environment to get more details. "
                          + str(e))

        self.assertTrue(os.access(iso_file, os.R_OK))

        self.logger.info('iso file %s successfully generated.', iso_file)

    def setUp(self): # pylint: disable=arguments-differ
        super().setUp()
        self.logger = self.log

        prebuiltDir = self.scratch_file('prebuilt')
        if not os.path.isdir(prebuiltDir):
            os.mkdir(prebuiltDir, mode=0o775)

        bits_zip_file = self.scratch_file('prebuilt',
                                          'bits-%d-%s.zip'
                                          %(self.BITS_INTERNAL_VER,
                                            self.BITS_COMMIT_HASH))
        grub_tar_file = self.scratch_file('prebuilt',
                                          'bits-%d-%s-grub.tar.gz'
                                          %(self.BITS_INTERNAL_VER,
                                            self.BITS_COMMIT_HASH))

        # extract the bits artifact in the temp working directory
        self.archive_extract(self.ASSET_BITS, sub_dir='prebuilt', format='zip')

        # extract the bits software in the temp working directory
        self.archive_extract(bits_zip_file)
        self.archive_extract(grub_tar_file)

        self.copy_test_scripts()
        self.copy_bits_config()
        self.generate_bits_iso()

    def parse_log(self):
        """parse the log generated by running bits tests and
           check for failures.
        """
        debugconf = self.scratch_file(self._debugcon_log)
        log = ""
        with open(debugconf, 'r', encoding='utf-8') as filehandle:
            log = filehandle.read()

        matchiter = re.finditer(r'(.*Summary: )(\d+ passed), (\d+ failed).*',
                                log)
        for match in matchiter:
            # verify that no test cases failed.
            try:
                self.assertEqual(match.group(3).split()[0], '0',
                                 'Some bits tests seems to have failed. ' \
                                 'Please check the test logs for more info.')
            except AssertionError as e:
                self._print_log(log)
                raise e
            else:
                if os.getenv('V') or os.getenv('BITS_DEBUG'):
                    self._print_log(log)

    def tearDown(self):
        """
           Lets do some cleanups.
        """
        if self._vm:
            self.assertFalse(not self._vm.is_running)
        super().tearDown()

    def test_acpi_smbios_bits(self):
        """The main test case implementation."""

        self.set_machine('pc')
        iso_file = self.scratch_file('bits-%d.iso' % self.BITS_INTERNAL_VER)

        self.assertTrue(os.access(iso_file, os.R_OK))

        self._vm = QEMUBitsMachine(binary=self.qemu_bin,
                                   base_temp_dir=self.workdir,
                                   debugcon_log=self._debugcon_log,
                                   debugcon_addr=self._debugcon_addr)

        self._vm.add_args('-cdrom', '%s' %iso_file)
        # the vm needs to be run under icount so that TCG emulation is
        # consistent in terms of timing. smilatency tests have consistent
        # timing requirements.
        self._vm.add_args('-icount', 'auto')
        # currently there is no support in bits for recognizing 64-bit SMBIOS
        # entry points. QEMU defaults to 64-bit entry points since the
        # upstream commit bf376f3020 ("hw/i386/pc: Default to use SMBIOS 3.0
        # for newer machine models"). Therefore, enforce 32-bit entry point.
        self._vm.add_args('-machine', 'smbios-entry-point-type=32')

        # enable console logging
        self._vm.set_console()
        self._vm.launch()


        # biosbits has been configured to run all the specified test suites
        # in batch mode and then automatically initiate a vm shutdown.
        self._vm.event_wait('SHUTDOWN', timeout=BITS_TIMEOUT)
        self._vm.wait(timeout=None)
        self.logger.debug("Checking console output ...")
        self.parse_log()

if __name__ == '__main__':
    QemuSystemTest.main()
