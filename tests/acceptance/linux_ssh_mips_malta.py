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
import paramiko
import time

from avocado import skipIf
from avocado_qemu import Test
from avocado.utils import process
from avocado.utils import archive


class LinuxSSH(Test):

    timeout = 150 # Not for 'configure --enable-debug --enable-debug-tcg'

    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '
    VM_IP = '127.0.0.1'

    IMAGE_INFO = {
        'be': {
            'image_url': 'https://people.debian.org/~aurel32/qemu/mips/'
                         'debian_wheezy_mips_standard.qcow2',
            'image_hash': '8987a63270df67345b2135a6b7a4885a35e392d5',
            'rsa_hostkey': b'AAAAB3NzaC1yc2EAAAADAQABAAABAQCca1VitiyLAdQOld'
                           b'zT43IOEVJZ0wHD78GJi8wDAjMiYWUzNSSn0rXGQsINHuH5'
                           b'IlF+kBZsHinb/FtKCAyS9a8uCHhQI4SuB4QhAb0+39MlUw'
                           b'Mm0CLkctgM2eUUZ6MQMQvDlqnue6CCkxN62EZYbaxmby7j'
                           b'CQa1125o1HRKBvdGm2zrJWxXAfA+f1v6jHLyE8Jnu83eQ+'
                           b'BFY25G+Vzx1PVc3zQBwJ8r0NGTRqy2//oWQP0h+bMsgeFe'
                           b'KH/J3RJM22vg6+I4JAdBFcxnK+l781h1FuRxOn4O/Xslbg'
                           b'go6WtB4V4TOsw2E/KfxI5IZ/icxF+swVcnvF46Hf3uQc/0'
                           b'BBqb',
        },
        'le': {
            'image_url': 'https://people.debian.org/~aurel32/qemu/mipsel/'
                         'debian_wheezy_mipsel_standard.qcow2',
            'image_hash': '7866764d9de3ef536ffca24c9fb9f04ffdb45802',
            'rsa_hostkey': b'AAAAB3NzaC1yc2EAAAADAQABAAABAQClXJlBT71HL5yKvv'
                           b'gfC7jmxSWx5zSBCzET6CLZczwAafSIs7YKfNOy/dQTxhuk'
                           b'yIGFUugZFoF3E9PzdhunuyvyTd56MPoNIqFbb5rGokwU5I'
                           b'TOx3dBHZR0mClypL6MVrwe0bsiIb8GhF1zioNwcsaAZnAi'
                           b'KfXStVDtXvn/kLLq+xLABYt48CC5KYWoFaCoICskLAY+qo'
                           b'L+LWyAnQisj4jAH8VSaSKIImFpfkHWEXPhHcC4ZBlDKtnH'
                           b'po9vhfCHgnfW3Pzrqmk8BI4HysqPFVmJWkJGlGUL+sGeg3'
                           b'ZZolAYuDXGuBrw8ooPJq2v2dOH+z6dyD2q/ypmAbyPqj5C'
                           b'rc8H',
        },
    }

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

    def ssh_connect(self, username, password, rsa_hostkey_b64=None):
        self.ssh_logger = logging.getLogger('ssh')
        self.ssh_username = username
        self.ssh_ps1 = '# ' if username is 'root' else '$ '
        self.ssh_client = paramiko.SSHClient()
        port = self.get_portfwd()
        if rsa_hostkey_b64:
            rsa_hostkey_bin = base64.b64decode(rsa_hostkey_b64)
            rsa_hostkey = paramiko.RSAKey(data = rsa_hostkey_bin)
            ipport = '[%s]:%s' % (self.VM_IP, port)
            self.ssh_logger.debug('ipport ' + ipport)
            self.ssh_client.get_host_keys().add(ipport, 'ssh-rsa', rsa_hostkey)
        for i in range(10):
            try:
                self.ssh_client.connect(self.VM_IP, int(port),
                                        username, password, banner_timeout=90)
                self.ssh_logger.info("Entering interactive session.")
                return
            except:
                time.sleep(4)
                pass
        self.fail("sshd timeout")

    def ssh_disconnect_vm(self):
        self.ssh_client.close()

    def ssh_command(self, command, is_root=True):
        self.ssh_logger.info(self.ssh_ps1 + command)
        stdin, stdout, stderr = self.ssh_client.exec_command(command)
        stdout_lines = [line.strip('\n') for line in stdout]
        for line in stdout_lines:
            self.ssh_logger.info(line)
        stderr_lines = [line.strip('\n') for line in stderr]
        for line in stderr_lines:
            self.ssh_logger.warning(line)
        return stdout_lines, stderr_lines

    def boot_debian_wheezy_image_and_ssh_login(self, endianess, kernel_path):
        image_url = self.IMAGE_INFO[endianess]['image_url']
        image_hash = self.IMAGE_INFO[endianess]['image_hash']
        image_path = self.fetch_asset(image_url, asset_hash=image_hash)
        rsa_hostkey_b64 = self.IMAGE_INFO[endianess]['rsa_hostkey']

        self.vm.set_machine('malta')
        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE
                               + 'console=ttyS0 root=/dev/sda1')
        self.vm.add_args('-no-reboot',
                         '-kernel', kernel_path,
                         '-append', kernel_command_line,
                         '-hda', image_path,
                         '-netdev', 'user,id=vnet,hostfwd=:127.0.0.1:0-:22',
                         '-device', 'pcnet,netdev=vnet')
        self.vm.launch()

        self.log.info('VM launched, waiting for sshd')
        console_pattern = 'Starting OpenBSD Secure Shell server: sshd'
        self.wait_for_console_pattern(console_pattern)
        self.log.info('sshd ready')

        self.ssh_connect('root', 'root', rsa_hostkey_b64=rsa_hostkey_b64)

    def shutdown_via_ssh(self):
        self.ssh_command('poweroff')
        self.ssh_disconnect_vm()
        self.wait_for_console_pattern('Power down')

    def run_common_commands(self):
        stdout, stderr = self.ssh_command('lspci -d 11ab:4620')
        self.assertIn(True, ["GT-64120" in line for line in stdout])

        stdout, stderr = self.ssh_command('cat /sys/bus/i2c/devices/i2c-0/name')
        self.assertIn(True, ["SMBus PIIX4 adapter" in line
                             for line in stdout])

        stdout, stderr = self.ssh_command('cat /proc/mtd')
        self.assertIn(True, ["YAMON" in line
                             for line in stdout])

        # Empty 'Board Config'
        stdout, stderr = self.ssh_command('md5sum /dev/mtd2ro')
        self.assertIn(True, ["0dfbe8aa4c20b52e1b8bf3cb6cbdf193" in line
                             for line in stdout])

    def do_test_mips_malta(self, endianess, kernel_path, uname_m):
        self.boot_debian_wheezy_image_and_ssh_login(endianess, kernel_path)

        stdout, stderr = self.ssh_command('uname -a')
        self.assertIn(True, [uname_m + " GNU/Linux" in line for line in stdout])

        self.run_common_commands()
        self.shutdown_via_ssh()

    @skipIf(os.getenv('CONTINUOUS_INTEGRATION'), 'Running on Travis-CI')
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

        self.do_test_mips_malta('be', kernel_path, 'mips')

    @skipIf(os.getenv('CONTINUOUS_INTEGRATION'), 'Running on Travis-CI')
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

        self.do_test_mips_malta('le', kernel_path, 'mips')

    @skipIf(os.getenv('CONTINUOUS_INTEGRATION'), 'Running on Travis-CI')
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
        self.do_test_mips_malta('be', kernel_path, 'mips64')

    @skipIf(os.getenv('CONTINUOUS_INTEGRATION'), 'Running on Travis-CI')
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
        self.do_test_mips_malta('le', kernel_path, 'mips64')
