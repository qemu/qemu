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
from avocado_qemu import wait_for_console_pattern
from avocado.utils import process
from avocado.utils import archive
from avocado.utils import ssh


class LinuxSSH(Test):

    timeout = 150 # Not for 'configure --enable-debug --enable-debug-tcg'

    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '
    VM_IP = '127.0.0.1'

    BASE_URL = 'https://people.debian.org/~aurel32/qemu/'
    IMAGE_INFO = {
        'be': {'base_url': 'mips',
               'image_name': 'debian_wheezy_mips_standard.qcow2',
               'image_hash': '8987a63270df67345b2135a6b7a4885a35e392d5',
               'kernel_hash': {
                   32: '592e384a4edc16dade52a6cd5c785c637bcbc9ad',
                   64: 'db6eea7de35d36c77d8c165b6bcb222e16eb91db'}
              },
        'le': {'base_url': 'mipsel',
               'image_name': 'debian_wheezy_mipsel_standard.qcow2',
               'image_hash': '7866764d9de3ef536ffca24c9fb9f04ffdb45802',
               'kernel_hash': {
                   32: 'a66bea5a8adaa2cb3d36a1d4e0ccdb01be8f6c2a',
                   64: '6a7f77245acf231415a0e8b725d91ed2f3487794'}
              }
        }
    CPU_INFO = {
        32: {'cpu': 'MIPS 24Kc', 'kernel_release': '3.2.0-4-4kc-malta'},
        64: {'cpu': 'MIPS 20Kc', 'kernel_release': '3.2.0-4-5kc-malta'}
        }

    def get_url(self, endianess, path=''):
        qkey = {'le': 'el', 'be': ''}
        return '%s/mips%s/%s' % (self.BASE_URL, qkey[endianess], path)

    def get_image_info(self, endianess):
        dinfo = self.IMAGE_INFO[endianess]
        image_url = self.get_url(endianess, dinfo['image_name'])
        image_hash = dinfo['image_hash']
        return (image_url, image_hash)

    def get_kernel_info(self, endianess, wordsize):
        minfo = self.CPU_INFO[wordsize]
        kernel_url = self.get_url(endianess,
                                  'vmlinux-%s' % minfo['kernel_release'])
        kernel_hash = self.IMAGE_INFO[endianess]['kernel_hash'][wordsize]
        return kernel_url, kernel_hash

    @skipUnless(ssh.SSH_CLIENT_BINARY, 'No SSH client available')
    @skipUnless(os.getenv('AVOCADO_TIMEOUT_EXPECTED'), 'Test might timeout')
    def setUp(self):
        super(LinuxSSH, self).setUp()

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
        self.fail("ssh connection timeout")

    def ssh_disconnect_vm(self):
        self.ssh_session.quit()

    def ssh_command(self, command, is_root=True):
        self.ssh_logger.info(command)
        result = self.ssh_session.cmd(command)
        stdout_lines = [line.rstrip() for line
                        in result.stdout_text.splitlines()]
        for line in stdout_lines:
            self.ssh_logger.info(line)
        stderr_lines = [line.rstrip() for line
                        in result.stderr_text.splitlines()]
        for line in stderr_lines:
            self.ssh_logger.warning(line)
        return stdout_lines, stderr_lines

    def boot_debian_wheezy_image_and_ssh_login(self, endianess, kernel_path):
        image_url, image_hash = self.get_image_info(endianess)
        image_path = self.fetch_asset(image_url, asset_hash=image_hash)

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
        wait_for_console_pattern(self, console_pattern, 'Oops')
        self.log.info('sshd ready')

        self.ssh_connect('root', 'root')

    def shutdown_via_ssh(self):
        self.ssh_command('poweroff')
        self.ssh_disconnect_vm()
        wait_for_console_pattern(self, 'Power down', 'Oops')

    def ssh_command_output_contains(self, cmd, exp):
        stdout, _ = self.ssh_command(cmd)
        for line in stdout:
            if exp in line:
                break
        else:
            self.fail('"%s" output does not contain "%s"' % (cmd, exp))

    def run_common_commands(self, wordsize):
        self.ssh_command_output_contains(
            'cat /proc/cpuinfo',
            self.CPU_INFO[wordsize]['cpu'])
        self.ssh_command_output_contains(
            'uname -m',
            'mips')
        self.ssh_command_output_contains(
            'uname -r',
            self.CPU_INFO[wordsize]['kernel_release'])
        self.ssh_command_output_contains(
            'cat /proc/interrupts',
            'XT-PIC  timer')
        self.ssh_command_output_contains(
            'cat /proc/interrupts',
            'XT-PIC  i8042')
        self.ssh_command_output_contains(
            'cat /proc/interrupts',
            'XT-PIC  serial')
        self.ssh_command_output_contains(
            'cat /proc/interrupts',
            'XT-PIC  ata_piix')
        self.ssh_command_output_contains(
            'cat /proc/interrupts',
            'XT-PIC  eth0')
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
            ' : serial')
        self.ssh_command_output_contains(
            'cat /proc/ioports',
            ' : ata_piix')
        self.ssh_command_output_contains(
            'cat /proc/ioports',
            ' : piix4_smbus')
        self.ssh_command_output_contains(
            'lspci -d 11ab:4620',
            'GT-64120')
        self.ssh_command_output_contains(
            'cat /sys/bus/i2c/devices/i2c-0/name',
            'SMBus PIIX4 adapter')
        self.ssh_command_output_contains(
            'cat /proc/mtd',
            'YAMON')
        # Empty 'Board Config' (64KB)
        self.ssh_command_output_contains(
            'md5sum /dev/mtd2ro',
            '0dfbe8aa4c20b52e1b8bf3cb6cbdf193')

    def check_mips_malta(self, uname_m, endianess):
        wordsize = 64 if '64' in uname_m else 32
        kernel_url, kernel_hash = self.get_kernel_info(endianess, wordsize)
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)
        self.boot_debian_wheezy_image_and_ssh_login(endianess, kernel_path)

        stdout, _ = self.ssh_command('uname -a')
        self.assertIn(True, [uname_m + " GNU/Linux" in line for line in stdout])

        self.run_common_commands(wordsize)
        self.shutdown_via_ssh()
        # Wait for VM to shut down gracefully
        self.vm.wait()

    def test_mips_malta32eb_kernel3_2_0(self):
        """
        :avocado: tags=arch:mips
        :avocado: tags=endian:big
        :avocado: tags=device:pcnet32
        """
        self.check_mips_malta('mips', 'be')

    def test_mips_malta32el_kernel3_2_0(self):
        """
        :avocado: tags=arch:mipsel
        :avocado: tags=endian:little
        :avocado: tags=device:pcnet32
        """
        self.check_mips_malta('mips', 'le')

    def test_mips_malta64eb_kernel3_2_0(self):
        """
        :avocado: tags=arch:mips64
        :avocado: tags=endian:big
        :avocado: tags=device:pcnet32
        """
        self.check_mips_malta('mips64', 'be')

    def test_mips_malta64el_kernel3_2_0(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=endian:little
        :avocado: tags=device:pcnet32
        """
        self.check_mips_malta('mips64', 'le')
