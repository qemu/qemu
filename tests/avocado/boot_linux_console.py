# Functional test that boots a Linux kernel and checks the console
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import lzma
import gzip
import shutil

from avocado import skip
from avocado import skipUnless
from avocado import skipUnless
from avocado_qemu import QemuSystemTest
from avocado_qemu import exec_command
from avocado_qemu import exec_command_and_wait_for_pattern
from avocado_qemu import interrupt_interactive_console_until_pattern
from avocado_qemu import wait_for_console_pattern
from avocado.utils import process
from avocado.utils import archive

class LinuxKernelTest(QemuSystemTest):
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    def extract_from_deb(self, deb, path):
        """
        Extracts a file from a deb package into the test workdir

        :param deb: path to the deb archive
        :param path: path within the deb archive of the file to be extracted
        :returns: path of the extracted file
        """
        cwd = os.getcwd()
        os.chdir(self.workdir)
        file_path = process.run("ar t %s" % deb).stdout_text.split()[2]
        process.run("ar x %s %s" % (deb, file_path))
        archive.extract(file_path, self.workdir)
        os.chdir(cwd)
        # Return complete path to extracted file.  Because callers to
        # extract_from_deb() specify 'path' with a leading slash, it is
        # necessary to use os.path.relpath() as otherwise os.path.join()
        # interprets it as an absolute path and drops the self.workdir part.
        return os.path.normpath(os.path.join(self.workdir,
                                             os.path.relpath(path, '/')))

    def extract_from_rpm(self, rpm, path):
        """
        Extracts a file from an RPM package into the test workdir.

        :param rpm: path to the rpm archive
        :param path: path within the rpm archive of the file to be extracted
                     needs to be a relative path (starting with './') because
                     cpio(1), which is used to extract the file, expects that.
        :returns: path of the extracted file
        """
        cwd = os.getcwd()
        os.chdir(self.workdir)
        process.run("rpm2cpio %s | cpio -id %s" % (rpm, path), shell=True)
        os.chdir(cwd)
        return os.path.normpath(os.path.join(self.workdir, path))

class BootLinuxConsole(LinuxKernelTest):
    """
    Boots a Linux kernel and checks that the console is operational and the
    kernel command line is properly passed from QEMU to the kernel
    """
    timeout = 90

    def test_x86_64_pc(self):
        """
        :avocado: tags=arch:x86_64
        :avocado: tags=machine:pc
        """
        kernel_url = ('https://archives.fedoraproject.org/pub/archive/fedora'
                      '/linux/releases/29/Everything/x86_64/os/images/pxeboot'
                      '/vmlinuz')
        kernel_hash = '23bebd2680757891cf7adedb033532163a792495'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    def test_arm_virt(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:virt
        :avocado: tags=accel:tcg
        """
        kernel_url = ('https://archives.fedoraproject.org/pub/archive/fedora'
                      '/linux/releases/29/Everything/armhfp/os/images/pxeboot'
                      '/vmlinuz')
        kernel_hash = 'e9826d741b4fb04cadba8d4824d1ed3b7fb8b4d4'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0')
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    @skipUnless(os.getenv('AVOCADO_TIMEOUT_EXPECTED'), 'Test might timeout')
    def test_arm_quanta_gsj(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:quanta-gsj
        :avocado: tags=accel:tcg
        """
        # 25 MiB compressed, 32 MiB uncompressed.
        image_url = (
                'https://github.com/hskinnemoen/openbmc/releases/download/'
                '20200711-gsj-qemu-0/obmc-phosphor-image-gsj.static.mtd.gz')
        image_hash = '14895e634923345cb5c8776037ff7876df96f6b1'
        image_path_gz = self.fetch_asset(image_url, asset_hash=image_hash)
        image_name = 'obmc.mtd'
        image_path = os.path.join(self.workdir, image_name)
        archive.gzip_uncompress(image_path_gz, image_path)

        self.vm.set_console()
        drive_args = 'file=' + image_path + ',if=mtd,bus=0,unit=0'
        self.vm.add_args('-drive', drive_args)
        self.vm.launch()

        # Disable drivers and services that stall for a long time during boot,
        # to avoid running past the 90-second timeout. These may be removed
        # as the corresponding device support is added.
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + (
                'console=${console} '
                'mem=${mem} '
                'initcall_blacklist=npcm_i2c_bus_driver_init '
                'systemd.mask=systemd-random-seed.service '
                'systemd.mask=dropbearkey.service '
        )

        self.wait_for_console_pattern('> BootBlock by Nuvoton')
        self.wait_for_console_pattern('>Device: Poleg BMC NPCM730')
        self.wait_for_console_pattern('>Skip DDR init.')
        self.wait_for_console_pattern('U-Boot ')
        interrupt_interactive_console_until_pattern(
                self, 'Hit any key to stop autoboot:', 'U-Boot>')
        exec_command_and_wait_for_pattern(
                self, "setenv bootargs ${bootargs} " + kernel_command_line,
                'U-Boot>')
        exec_command_and_wait_for_pattern(
                self, 'run romboot', 'Booting Kernel from flash')
        self.wait_for_console_pattern('Booting Linux on physical CPU 0x0')
        self.wait_for_console_pattern('CPU1: thread -1, cpu 1, socket 0')
        self.wait_for_console_pattern('OpenBMC Project Reference Distro')
        self.wait_for_console_pattern('gsj login:')

    def test_arm_quanta_gsj_initrd(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:quanta-gsj
        :avocado: tags=accel:tcg
        """
        initrd_url = (
                'https://github.com/hskinnemoen/openbmc/releases/download/'
                '20200711-gsj-qemu-0/obmc-phosphor-initramfs-gsj.cpio.xz')
        initrd_hash = '98fefe5d7e56727b1eb17d5c00311b1b5c945300'
        initrd_path = self.fetch_asset(initrd_url, asset_hash=initrd_hash)
        kernel_url = (
                'https://github.com/hskinnemoen/openbmc/releases/download/'
                '20200711-gsj-qemu-0/uImage-gsj.bin')
        kernel_hash = 'fa67b2f141d56d39b3c54305c0e8a899c99eb2c7'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)
        dtb_url = (
                'https://github.com/hskinnemoen/openbmc/releases/download/'
                '20200711-gsj-qemu-0/nuvoton-npcm730-gsj.dtb')
        dtb_hash = '18315f7006d7b688d8312d5c727eecd819aa36a4'
        dtb_path = self.fetch_asset(dtb_url, asset_hash=dtb_hash)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200n8 '
                               'earlycon=uart8250,mmio32,0xf0001000')
        self.vm.add_args('-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-dtb', dtb_path,
                         '-append', kernel_command_line)
        self.vm.launch()

        self.wait_for_console_pattern('Booting Linux on physical CPU 0x0')
        self.wait_for_console_pattern('CPU1: thread -1, cpu 1, socket 0')
        self.wait_for_console_pattern(
                'Give root password for system maintenance')

    def test_arm_ast2600_debian(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:rainier-bmc
        """
        deb_url = ('http://snapshot.debian.org/archive/debian/'
                   '20220606T211338Z/'
                   'pool/main/l/linux/'
                   'linux-image-5.17.0-2-armmp_5.17.6-1%2Bb1_armhf.deb')
        deb_hash = '8acb2b4439faedc2f3ed4bdb2847ad4f6e0491f73debaeb7f660c8abe4dcdc0e'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash,
                                    algorithm='sha256')
        kernel_path = self.extract_from_deb(deb_path, '/boot/vmlinuz-5.17.0-2-armmp')
        dtb_path = self.extract_from_deb(deb_path,
                '/usr/lib/linux-image-5.17.0-2-armmp/aspeed-bmc-ibm-rainier.dtb')

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-net', 'nic')
        self.vm.launch()
        self.wait_for_console_pattern("Booting Linux on physical CPU 0xf00")
        self.wait_for_console_pattern("SMP: Total of 2 processors activated")
        self.wait_for_console_pattern("No filesystem could mount root")

