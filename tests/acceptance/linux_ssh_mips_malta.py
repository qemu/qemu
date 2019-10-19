# Functional test that boots a VM and run commands via a SSH session
#
# Copyright (c) Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import re
import base64
import logging
import time

from avocado import skipUnless
from avocado_qemu import Test
from avocado.utils import process
from avocado.utils import archive
from avocado.utils import ssh


class LinuxSSH(Test):

    timeout = 150 # Not for 'configure --enable-debug --enable-debug-tcg'

    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '
    VM_IP = '127.0.0.1'

    IMAGE_INFO = {
        'be': {'image_url': ('https://people.debian.org/~aurel32/qemu/mips/'
                             'debian_wheezy_mips_standard.qcow2'),
               'image_hash': '8987a63270df67345b2135a6b7a4885a35e392d5'},
        'le': {'image_url': ('https://people.debian.org/~aurel32/qemu/mipsel/'
                             'debian_wheezy_mipsel_standard.qcow2'),
               'image_hash': '7866764d9de3ef536ffca24c9fb9f04ffdb45802'}
        }


    @skipUnless(ssh.SSH_CLIENT_BINARY, 'No SSH client available')
    @skipUnless(os.getenv('AVOCADO_TIMEOUT_EXPECTED'), 'Test might timeout')
    def setUp(self):
        super(LinuxSSH, self).setUp()

    def wait_for_console_pattern(self, success_message,
                                 failure_message='Oops'):
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

    def get_portfwd(self):
        res = self.vm.command('human-monitor-command',
                              command_line='info usernet')
        line = res.split('\r\n')[2]
        port = re.split(r'.*TCP.HOST_FORWARD.*127\.0\.0\.1 (\d+)\s+10\..*',
                        line)[1]
        self.log.debug("sshd listening on port:" + port)
        return port

    def ssh_connect(self, username, password):
        self.ssh_logger = logging.getLogger('ssh')
        port = self.get_portfwd()
        self.ssh_session = ssh.Session(self.VM_IP, port=int(port),
                                       user=username, password=password)
        for i in range(10):
            try:
                self.ssh_session.connect()
                return
            except:
                time.sleep(4)
                pass
        self.fail("sshd timeout")

    def ssh_disconnect_vm(self):
        self.ssh_session.quit()

    def ssh_command(self, command, is_root=True):
        self.ssh_logger.info(command)
        result = self.ssh_session.cmd(command)
        stdout_lines = [line.rstrip() for line in result.stdout_text.splitlines()]
        for line in stdout_lines:
            self.ssh_logger.info(line)
        stderr_lines = [line.rstrip() for line in result.stderr_text.splitlines()]
        for line in stderr_lines:
            self.ssh_logger.warning(line)
        return stdout_lines, stderr_lines

    def boot_debian_wheezy_image_and_ssh_login(self, endianess, kernel_path):
        image_url = self.IMAGE_INFO[endianess]['image_url']
        image_hash = self.IMAGE_INFO[endianess]['image_hash']
        image_path = self.fetch_asset(image_url, asset_hash=image_hash)

        self.vm.set_machine('malta')
        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE
                               + 'console=ttyS0 root=/dev/sda1')
        self.vm.add_args('-no-reboot',
                         '-kernel', kernel_path,
                         '-append', kernel_command_line,
                         '-drive', 'file=%s,snapshot=on' % image_path,
                         '-netdev', 'user,id=vnet,hostfwd=:127.0.0.1:0-:22',
                         '-device', 'pcnet,netdev=vnet')
        self.vm.launch()

        self.log.info('VM launched, waiting for sshd')
        console_pattern = 'Starting OpenBSD Secure Shell server: sshd'
        self.wait_for_console_pattern(console_pattern)
        self.log.info('sshd ready')

        self.ssh_connect('root', 'root')

    def shutdown_via_ssh(self):
        self.ssh_command('poweroff')
        self.ssh_disconnect_vm()
        self.wait_for_console_pattern('Power down')

    def ssh_command_output_contains(self, cmd, exp):
        stdout, _ = self.ssh_command(cmd)
        for line in stdout:
            if exp in line:
                break
        else:
            self.fail('"%s" output does not contain "%s"' % (cmd, exp))

    def run_common_commands(self):
        self.ssh_command_output_contains(
            'cat /proc/cpuinfo',
            '24Kc')
        self.ssh_command_output_contains(
            'uname -m',
            'mips')
        self.ssh_command_output_contains(
            'uname -r',
            '3.2.0-4-4kc-malta')
        self.ssh_command_output_contains(
            'cat /proc/interrupts',
            'timer')
        self.ssh_command_output_contains(
            'cat /proc/interrupts',
            'i8042')
        self.ssh_command_output_contains(
            'cat /proc/interrupts',
            'serial')
        self.ssh_command_output_contains(
            'cat /proc/interrupts',
            'ata_piix')
        self.ssh_command_output_contains(
            'cat /proc/interrupts',
            'eth0')
        self.ssh_command_output_contains(
            'cat /proc/interrupts',
            'eth0')
        self.ssh_command_output_contains(
            'cat /proc/devices',
            'input')
        self.ssh_command_output_contains(
            'cat /proc/devices',
            'usb')
        self.ssh_command_output_contains(
            'cat /proc/devices',
            'fb')
        self.ssh_command_output_contains(
            'cat /proc/ioports',
            'serial')
        self.ssh_command_output_contains(
            'cat /proc/ioports',
            'ata_piix')
        self.ssh_command_output_contains(
            'cat /proc/ioports',
            'piix4_smbus')
        self.ssh_command_output_contains(
            'lspci -d 11ab:4620',
            'GT-64120')
        self.ssh_command_output_contains(
            'cat /sys/bus/i2c/devices/i2c-0/name',
            'SMBus PIIX4 adapter')
        self.ssh_command_output_contains(
            'cat /proc/mtd',
            'YAMON')
        # Empty 'Board Config'
        self.ssh_command_output_contains(
            'md5sum /dev/mtd2ro',
            '0dfbe8aa4c20b52e1b8bf3cb6cbdf193')

    def check_mips_malta(self, endianess, kernel_path, uname_m):
        self.boot_debian_wheezy_image_and_ssh_login(endianess, kernel_path)

        stdout, _ = self.ssh_command('uname -a')
        self.assertIn(True, [uname_m + " GNU/Linux" in line for line in stdout])

        self.run_common_commands()
        self.shutdown_via_ssh()

    def test_mips_malta32eb_kernel3_2_0(self):
        """
        :avocado: tags=arch:mips
        :avocado: tags=machine:malta
        :avocado: tags=endian:big
        :avocado: tags=device:pcnet32
        """
        kernel_url = ('https://people.debian.org/~aurel32/qemu/mips/'
                      'vmlinux-3.2.0-4-4kc-malta')
        kernel_hash = '592e384a4edc16dade52a6cd5c785c637bcbc9ad'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.check_mips_malta('be', kernel_path, 'mips')

    def test_mips_malta32el_kernel3_2_0(self):
        """
        :avocado: tags=arch:mipsel
        :avocado: tags=machine:malta
        :avocado: tags=endian:little
        :avocado: tags=device:pcnet32
        """
        kernel_url = ('https://people.debian.org/~aurel32/qemu/mipsel/'
                      'vmlinux-3.2.0-4-4kc-malta')
        kernel_hash = 'a66bea5a8adaa2cb3d36a1d4e0ccdb01be8f6c2a'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.check_mips_malta('le', kernel_path, 'mips')

    def test_mips_malta64eb_kernel3_2_0(self):
        """
        :avocado: tags=arch:mips64
        :avocado: tags=machine:malta
        :avocado: tags=endian:big
        :avocado: tags=device:pcnet32
        """
        kernel_url = ('https://people.debian.org/~aurel32/qemu/mips/'
                      'vmlinux-3.2.0-4-5kc-malta')
        kernel_hash = 'db6eea7de35d36c77d8c165b6bcb222e16eb91db'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)
        self.check_mips_malta('be', kernel_path, 'mips64')

    def test_mips_malta64el_kernel3_2_0(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=machine:malta
        :avocado: tags=endian:little
        :avocado: tags=device:pcnet32
        """
        kernel_url = ('https://people.debian.org/~aurel32/qemu/mipsel/'
                      'vmlinux-3.2.0-4-5kc-malta')
        kernel_hash = '6a7f77245acf231415a0e8b725d91ed2f3487794'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)
        self.check_mips_malta('le', kernel_path, 'mips64')
