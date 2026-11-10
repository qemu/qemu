#!/usr/bin/env python3
#
# Test of the ccid-emulated device. This roughly follows the lines
# suggested in our documentation of it, but deviates a bit because
# the buildroot-based rootfs we use doesn't have glibc to use the
# certutil + NSS validation process. Instead we use opensc's
# pkcs15-tool to read the certs off the card and openssl to check them.
#
# Copyright (c) 2026 Linaro Ltd
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import shutil
import subprocess

from qemu_test import (Asset, exec_command_and_wait_for_pattern,
                       skipIfMissingCommands)
from qemu_test.tuxruntest import TuxRunBaselineTest


class CCIDEmulatedTest(TuxRunBaselineTest):

    ASSET_KERNEL = Asset(
        'https://storage.tuxboot.com/kernels/20261110/6.18.54/x86_64/bzImage',
        'ba98a8a07d5f3f3e20119341059c0f0096fda499d30e30049c1ea64553b7e7c2')
    ASSET_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20261110/x86_64/rootfs.ext4.zst',
        '267172d47dc9594a8f07ec7fec05dc17b0fc21c64d5b635fccd6455128a2eb58')

    def create_certs(self):
        # Create a CA and certificates in a dir under our test's
        # working dir (removing any stale contents it had first). We
        # return the db_spec that the ccid-card-emulated options need,
        # plus the filename of the exported CA cert

        certdir = self.scratch_file("certs")
        shutil.rmtree(certdir, ignore_errors=True)
        os.mkdir(certdir)

        db_spec = f'sql:{certdir}'

        # certutil will by default block asking for some keyboard
        # input to get randomness; create a noise-file to pass to it
        # instead so it will work non-interactively
        noise_file = self.scratch_file("noise.bin")
        with open(noise_file, 'wb') as f:
            f.write(os.urandom(2048))

        subprocess.check_call(['certutil', '-N', '-d', db_spec,
                               '--empty-password'])

        # We explicitly specify serial numbers (420, 421, 422, 423),
        # because certutil's default is to generate a serial based on
        # the current time, and when run from a script it will end up
        # repeating the same serial for two certificates, which causes
        # it to error out.
        subprocess.check_call(['certutil', '-S', '-d', db_spec,
                               '-z', noise_file,
                               '-s', "CN=Fake Smart Card CA",
                               '-x', '-t', 'TC,TC,TC',
                               '-m', '420',
                               '--keyUsage',
                               'certSigning,crlSigning,critical',
                               '-n', 'fake-smartcard-ca'],
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL)

        subprocess.check_call(['certutil', '-S', '-d', db_spec,
                               '-z', noise_file,
                               '-t', ',,',
                               '-s', "CN=John Doe",
                               '-n', 'id-cert',
                               '-m', '421',
                               '-c', 'fake-smartcard-ca'],
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL)

        subprocess.check_call(['certutil', '-S', '-d', db_spec,
                               '-z', noise_file,
                               '-t', ',,',
                               '-s', "CN=John Doe (signing)",
                               '--nsCertType', 'smime',
                               '-n', 'signing-cert',
                               '-m', '422',
                               '-c', 'fake-smartcard-ca'],
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL)

        subprocess.check_call(['certutil', '-S', '-d', db_spec,
                               '-z', noise_file,
                               '-t', ',,',
                               '-s', "CN=John Doe (encryption)",
                               '--nsCertType', 'sslClient',
                               '-n', 'encryption-cert',
                               '-m', '423',
                               '-c', 'fake-smartcard-ca'],
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL)

        # Now export the CA cert to a file
        ca_cert = self.scratch_file("fake-smartcard-ca.pem")
        subprocess.check_call(['certutil', '-L', '-a', '-d', db_spec,
                               '-o', ca_cert, '-n', 'fake-smartcard-ca'])

        return (db_spec, ca_cert)

    def run_cmd(self, cmd):
        # Run a command, and wait for the following prompt
        exec_command_and_wait_for_pattern(self, cmd, '# ')

    @skipIfMissingCommands('certutil')
    def test_ccid(self):
        self.set_machine('q35')
        self.require_device('usb-ccid')
        self.require_device('ccid-card-emulated')
        self.cpu = 'Nehalem'
        self.root = 'sda'

        (kernel, disk, _, _) = self.fetch_tuxrun_assets(self.ASSET_KERNEL,
                                                        self.ASSET_ROOTFS)

        (db_spec, ca_cert) = self.create_certs()

        certspec = f'db={db_spec},cert1=id-cert,cert2=signing-cert,cert3=encryption-cert'

        self.vm.add_args('-m', '1G', '-usb',
                         '-device', 'usb-ccid',
                         '-device',
                         f'ccid-card-emulated,backend=certificates,{certspec}',
                         '-fw_cfg', f'opt/org.qemu/ccid-test,file={ca_cert}')

        self.prepare_run(None, kernel, disk,
                         drive="driver=ide-hd,bus=ide.0,unit=0")
        self.vm.launch()

        ps1 = 'root@tuxtest:~#'
        self.wait_for_console_pattern(self.kcmd_line)
        self.wait_for_console_pattern('tuxtest login:')
        exec_command_and_wait_for_pattern(self, 'root', ps1)

        self.run_cmd('cp '
                     '/sys/firmware/qemu_fw_cfg/by_name/opt/org.qemu/ccid-test/raw '
                     '/tmp/fake-smartcard-ca.pem')

        # Start the daemon which actually talks to the smartcard
        self.run_cmd('pcscd -d')

        # list the certs; having this in the console log may help debugging
        self.run_cmd('pkcs15-tool --list-certificates')

        # Read each cert off the card, and get openssl to check that it
        # validates against our fake CA cert
        for cert in ('0001', '0002', '0003'):
            self.run_cmd('pkcs15-tool '
                         f'--read-certificate {cert} -o /tmp/cert-{cert}.pem')
            exec_command_and_wait_for_pattern(self,
                                              'openssl verify '
                                              '-CAfile '
                                              '/tmp/fake-smartcard-ca.pem '
                                              f'/tmp/cert-{cert}.pem',
                                              f'/tmp/cert-{cert}.pem: OK',
                                              failure_message='# ')
            self.wait_for_console_pattern('# ')


if __name__ == '__main__':
    CCIDEmulatedTest.main()
