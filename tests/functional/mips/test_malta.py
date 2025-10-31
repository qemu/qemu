#!/usr/bin/env python3
#
# Functional tests for the big-endian 32-bit MIPS Malta board
#
# Copyright (c) Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os

from qemu_test import LinuxKernelTest, Asset, wait_for_console_pattern
from qemu_test import skipFlakyTest
from qemu_test import exec_command_and_wait_for_pattern


def mips_run_common_commands(test, prompt='#'):
    exec_command_and_wait_for_pattern(test,
        'uname -m',
        'mips')
    exec_command_and_wait_for_pattern(test,
        'grep XT-PIC /proc/interrupts',
        'timer')
    wait_for_console_pattern(test, prompt)
    exec_command_and_wait_for_pattern(test,
        'grep XT-PIC /proc/interrupts',
        'serial')
    wait_for_console_pattern(test, prompt)
    exec_command_and_wait_for_pattern(test,
        'grep XT-PIC /proc/interrupts',
        'ata_piix')
    wait_for_console_pattern(test, prompt)
    exec_command_and_wait_for_pattern(test,
        'grep XT-PIC /proc/interrupts',
        'rtc')
    wait_for_console_pattern(test, prompt)
    exec_command_and_wait_for_pattern(test,
        'cat /proc/devices',
        'input')
    wait_for_console_pattern(test, prompt)
    exec_command_and_wait_for_pattern(test,
        'cat /proc/devices',
        'fb')
    wait_for_console_pattern(test, prompt)
    exec_command_and_wait_for_pattern(test,
        'cat /proc/ioports',
        ' : serial')
    wait_for_console_pattern(test, prompt)
    exec_command_and_wait_for_pattern(test,
        'cat /proc/ioports',
        ' : ata_piix')
    wait_for_console_pattern(test, prompt)

def mips_check_wheezy(test, kernel_path, image_path, kernel_command_line,
                      dl_file, hsum, nic='pcnet', cpuinfo='MIPS 24Kc'):
    test.require_netdev('user')
    test.require_device(nic)
    test.set_machine('malta')

    port=8080
    test.vm.add_args('-kernel', kernel_path,
                     '-append', kernel_command_line,
                     '-drive', 'file=%s,snapshot=on' % image_path,
                     '-netdev', 'user,id=n1' +
                                ',tftp=' + os.path.basename(kernel_path) +
                                ',hostfwd=tcp:127.0.0.1:0-:%d' % port,
                     '-device', f'{nic},netdev=n1',
                     '-no-reboot')
    test.vm.set_console()
    test.vm.launch()

    wait_for_console_pattern(test, 'login: ', 'Oops')
    exec_command_and_wait_for_pattern(test, 'root', 'Password:')
    exec_command_and_wait_for_pattern(test, 'root', ':~# ')
    mips_run_common_commands(test)

    exec_command_and_wait_for_pattern(test, 'cd /', '# ')
    test.check_http_download(dl_file, hsum, port,
                             pythoncmd='python -m SimpleHTTPServer')

    exec_command_and_wait_for_pattern(test, 'cat /proc/cpuinfo', cpuinfo)
    exec_command_and_wait_for_pattern(test, 'cat /proc/devices', 'usb')
    exec_command_and_wait_for_pattern(test, 'cat /proc/ioports',
                                      ' : piix4_smbus')
    exec_command_and_wait_for_pattern(test, 'lspci -d 11ab:4620',
                                      'GT-64120')
    exec_command_and_wait_for_pattern(test,
                                      'cat /sys/bus/i2c/devices/i2c-0/name',
                                      'SMBus PIIX4 adapter')
    exec_command_and_wait_for_pattern(test, 'cat /proc/mtd', 'YAMON')
    # Empty 'Board Config' (64KB)
    exec_command_and_wait_for_pattern(test, 'md5sum /dev/mtd2ro',
                                      '0dfbe8aa4c20b52e1b8bf3cb6cbdf193')


class MaltaMachineConsole(LinuxKernelTest):

    ASSET_KERNEL_2_63_2 = Asset(
        ('http://snapshot.debian.org/archive/debian/'
         '20130217T032700Z/pool/main/l/linux-2.6/'
         'linux-image-2.6.32-5-4kc-malta_2.6.32-48_mips.deb'),
        '16ca524148afb0626f483163e5edf352bc1ab0e4fc7b9f9d473252762f2c7a43')

    def test_mips_malta(self):
        kernel_path = self.archive_extract(
            self.ASSET_KERNEL_2_63_2,
            member='boot/vmlinux-2.6.32-5-4kc-malta')

        self.set_machine('malta')
        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    ASSET_KERNEL_4_5_0 = Asset(
        ('http://snapshot.debian.org/archive/debian/'
         '20160601T041800Z/pool/main/l/linux/'
         'linux-image-4.5.0-2-4kc-malta_4.5.5-1_mips.deb'),
        '526b17d5889840888b76fc2c36a0ebde182c9b1410a3a1e68203c3b160eb2027')

    ASSET_INITRD = Asset(
        ('https://github.com/groeck/linux-build-test/raw/'
         '8584a59ed9e5eb5ee7ca91f6d74bbb06619205b8/rootfs/'
         'mips/rootfs.cpio.gz'),
        'dcfe3a7fe3200da3a00d176b95caaa086495eb158f2bff64afc67d7e1eb2cddc')

    def test_mips_malta_cpio(self):
        self.require_netdev('user')
        self.set_machine('malta')
        self.require_device('pcnet')

        kernel_path = self.archive_extract(
            self.ASSET_KERNEL_4_5_0,
            member='boot/vmlinux-4.5.0-2-4kc-malta')
        initrd_path = self.uncompress(self.ASSET_INITRD)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE
                               + 'console=ttyS0 console=tty '
                               + 'rdinit=/sbin/init noreboot')
        self.vm.add_args('-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line,
                         '-netdev', 'user,id=n1,tftp=' + self.scratch_file('boot'),
                         '-device', 'pcnet,netdev=n1',
                         '-no-reboot')
        self.vm.launch()
        self.wait_for_console_pattern('Boot successful.')

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'BogoMIPS')
        exec_command_and_wait_for_pattern(self, 'uname -a',
                                                '4.5.0-2-4kc-malta #1 Debian')
        mips_run_common_commands(self)

        exec_command_and_wait_for_pattern(self, 'ip link set eth0 up',
                                          'eth0: link up')
        exec_command_and_wait_for_pattern(self,
                                          'ip addr add 10.0.2.15 dev eth0',
                                          '#')
        exec_command_and_wait_for_pattern(self, 'route add default eth0', '#')
        exec_command_and_wait_for_pattern(self,
                         'tftp -g -r vmlinux-4.5.0-2-4kc-malta 10.0.2.2', '#')
        exec_command_and_wait_for_pattern(self,
                                          'md5sum vmlinux-4.5.0-2-4kc-malta',
                                          'a98218a7efbdefb2dfdf9ecd08c98318')

        exec_command_and_wait_for_pattern(self, 'reboot',
                                                'reboot: Restarting system')
        # Wait for VM to shut down gracefully
        self.vm.wait()

    ASSET_WHEEZY_KERNEL = Asset(
        ('https://people.debian.org/~aurel32/qemu/mips/'
         'vmlinux-3.2.0-4-4kc-malta'),
        '0377fcda31299213c10b8e5babe7260ef99188b3ae1aca6f56594abb71e7f67e')

    ASSET_WHEEZY_DISK = Asset(
        ('https://people.debian.org/~aurel32/qemu/mips/'
         'debian_wheezy_mips_standard.qcow2'),
        'de03599285b8382ad309309a6c4869f6c6c42a5cfc983342bab9ec0dfa7849a2')

    @skipFlakyTest("https://gitlab.com/qemu-project/qemu/-/issues/3109")
    def test_wheezy(self):
        kernel_path = self.ASSET_WHEEZY_KERNEL.fetch()
        image_path = self.ASSET_WHEEZY_DISK.fetch()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE
                               + 'console=ttyS0 root=/dev/sda1')
        mips_check_wheezy(self,
            kernel_path, image_path, kernel_command_line, nic='e1000',
            dl_file='/boot/initrd.img-3.2.0-4-4kc-malta',
            hsum='ff0c0369143d9bbb9a6e6bc79322a2be535619df639e84103237f406e87493dc')


if __name__ == '__main__':
    LinuxKernelTest.main()
