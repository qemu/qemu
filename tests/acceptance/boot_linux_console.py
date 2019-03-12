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
import logging

from avocado_qemu import Test
from avocado.utils import process
from avocado.utils import archive


class BootLinuxConsole(Test):
    """
    Boots a Linux kernel and checks that the console is operational and the
    kernel command line is properly passed from QEMU to the kernel
    """

    timeout = 90

    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    def wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing'):
        """
        Waits for messages to appear on the console, while logging the content

        :param success_message: if this message appears, test succeeds
        :param failure_message: if this message appears, test fails
        """
        console = self.vm.console_socket.makefile()
        console_logger = logging.getLogger('console')
        while True:
            msg = console.readline()
            console_logger.debug(msg.strip())
            if success_message in msg:
                break
            if failure_message in msg:
                fail = 'Failure message found in console: %s' % failure_message
                self.fail(fail)

    def extract_from_deb(self, deb, path):
        """
        Extracts a file from a deb package into the test workdir

        :param deb: path to the deb archive
        :param file: path within the deb archive of the file to be extracted
        :returns: path of the extracted file
        """
        cwd = os.getcwd()
        os.chdir(self.workdir)
        process.run("ar x %s data.tar.gz" % deb)
        archive.extract("data.tar.gz", self.workdir)
        os.chdir(cwd)
        return self.workdir + path

    def test_x86_64_pc(self):
        """
        :avocado: tags=arch:x86_64
        :avocado: tags=machine:pc
        """
        kernel_url = ('https://download.fedoraproject.org/pub/fedora/linux/'
                      'releases/29/Everything/x86_64/os/images/pxeboot/vmlinuz')
        kernel_hash = '23bebd2680757891cf7adedb033532163a792495'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_machine('pc')
        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    def test_mips_malta(self):
        """
        :avocado: tags=arch:mips
        :avocado: tags=machine:malta
        :avocado: tags=endian:big
        """
        deb_url = ('http://snapshot.debian.org/archive/debian/'
                   '20130217T032700Z/pool/main/l/linux-2.6/'
                   'linux-image-2.6.32-5-4kc-malta_2.6.32-48_mips.deb')
        deb_hash = 'a8cfc28ad8f45f54811fc6cf74fc43ffcfe0ba04'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinux-2.6.32-5-4kc-malta')

        self.vm.set_machine('malta')
        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    def test_mips64el_malta(self):
        """
        This test requires the ar tool to extract "data.tar.gz" from
        the Debian package.

        The kernel can be rebuilt using this Debian kernel source [1] and
        following the instructions on [2].

        [1] http://snapshot.debian.org/package/linux-2.6/2.6.32-48/
            #linux-source-2.6.32_2.6.32-48
        [2] https://kernel-team.pages.debian.net/kernel-handbook/
            ch-common-tasks.html#s-common-official

        :avocado: tags=arch:mips64el
        :avocado: tags=machine:malta
        """
        deb_url = ('http://snapshot.debian.org/archive/debian/'
                   '20130217T032700Z/pool/main/l/linux-2.6/'
                   'linux-image-2.6.32-5-5kc-malta_2.6.32-48_mipsel.deb')
        deb_hash = '1aaec92083bf22fda31e0d27fa8d9a388e5fc3d5'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinux-2.6.32-5-5kc-malta')

        self.vm.set_machine('malta')
        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)
