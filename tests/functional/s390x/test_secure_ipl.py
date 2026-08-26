#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
"""
s390x Secure IPL functional test.

Validates s390x secure boot by preparing a signed guest image, booting with
secure-boot enabled, and verifying cryptographic validation results.
"""

from subprocess import check_call, DEVNULL

from qemu_test import QemuSystemTest, Asset, get_qemu_img
from qemu_test import exec_command_and_wait_for_pattern, exec_command
from qemu_test import wait_for_console_pattern, skipBigDataTest

class S390xSecureIpl(QemuSystemTest):
    """Test s390x Secure IPL (secure boot) functionality."""
    ASSET_F40_QCOW2 = Asset(
        ('https://archives.fedoraproject.org/pub/archive/'
         'fedora-secondary/releases/40/Server/s390x/images/'
         'Fedora-Server-KVM-40-1.14.s390x.qcow2'),
        '091c232a7301be14e19c76ce9a0c1cbd2be2c4157884a731e1fc4f89e7455a5f')

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.root_password = None
        self.qcow2_path = None
        self.cert_path = None
        self.prompt = None

    def _create_certificate(self, vm):
        """Generate x509 certificate"""
        exec_command_and_wait_for_pattern(self,
                                          'openssl version', 'OpenSSL 3.2.1 30',
                                          vm=vm)
        exec_command_and_wait_for_pattern(self,
                            'openssl req -new -x509 -newkey rsa:2048 '
                            '-keyout mykey.pem -outform PEM -out mycert.pem '
                            '-days 36500 -subj "/CN=My Name/" -nodes -verbose',
                            'Writing private key to \'mykey.pem\'', vm=vm)

    def _sign_binaries(self, vm):
        """Sign stage3 binary and kernel"""
        # Install kernel-devel (needed for sign-file)
        exec_command_and_wait_for_pattern(self,
                                'sudo dnf install kernel-devel-$(uname -r) -y',
                                'Complete!', vm=vm)
        wait_for_console_pattern(self, self.prompt, vm=vm)
        exec_command_and_wait_for_pattern(self,
                                    'ls /usr/src/kernels/$(uname -r)/scripts/',
                                    'sign-file', vm=vm)

        # Sign stage3 binary and kernel
        exec_command(self, '/usr/src/kernels/$(uname -r)/scripts/sign-file '
                    'sha256 mykey.pem mycert.pem /lib/s390-tools/stage3.bin',
                    vm=vm)
        wait_for_console_pattern(self, self.prompt, vm=vm)
        exec_command(self, '/usr/src/kernels/$(uname -r)/scripts/sign-file '
                    'sha256 mykey.pem mycert.pem /boot/vmlinuz-$(uname -r)',
                    vm=vm)
        wait_for_console_pattern(self, self.prompt, vm=vm)

    def _run_zipl_secure(self, vm):
        """Run zipl to prepare for secure boot"""
        exec_command_and_wait_for_pattern(self, 'zipl --secure 1 -VV', 'Done.',
                                          vm=vm)

    def _extract_certificate(self, vm):
        """Extract certificate from VM to host filesystem"""
        out = exec_command_and_wait_for_pattern(self, 'cat mycert.pem',
                                                '-----END CERTIFICATE-----',
                                                vm=vm)
        # strip first line to avoid console echo artifacts
        cert = "\n".join(out.decode("utf-8").splitlines()[1:])
        self.log.info("%s", cert)

        self.cert_path = self.scratch_file("mycert.pem")

        with open(self.cert_path, 'w', encoding="utf-8") as file_object:
            file_object.write(cert)

    def setup_s390x_secure_ipl(self):
        """
        Prepare a secure boot-enabled guest image.

        Boots a temporary VM to generate a certificate, sign boot components
        (stage3 and kernel), run zipl, and extract the certificate to host.
        """
        self.require_netdev('user')

        temp_vm = self.get_vm(name='sipl_setup')
        temp_vm.set_machine('s390-ccw-virtio')

        asset_path = self.ASSET_F40_QCOW2.fetch()
        self.qcow2_path = self.scratch_file('f40.qcow2')
        qemu_img = get_qemu_img(self)
        check_call([qemu_img, 'create', '-f', 'qcow2', '-b', asset_path,
                    '-F', 'qcow2', self.qcow2_path], stdout=DEVNULL, stderr=DEVNULL)

        temp_vm.set_console()
        temp_vm.add_args('-nographic',
                         '-accel', 'kvm',
                         '-m', '1024',
                         '-drive',
                         f'id=drive0,if=none,format=qcow2,file={self.qcow2_path}',
                         '-device', 'virtio-blk-ccw,drive=drive0,bootindex=1')
        temp_vm.launch()

        # Initial root account setup (Fedora first boot screen)
        self.root_password = 'fedora40password'
        wait_for_console_pattern(self, 'Please make a selection from the above',
                                 vm=temp_vm)
        exec_command_and_wait_for_pattern(self, '4', 'Password:', vm=temp_vm)
        exec_command_and_wait_for_pattern(self, self.root_password,
                                          'Password (confirm):', vm=temp_vm)
        exec_command_and_wait_for_pattern(self, self.root_password,
                                    'Please make a selection from the above',
                                    vm=temp_vm)

        # Login as root
        self.prompt = '[root@localhost ~]#'
        exec_command_and_wait_for_pattern(self, 'c', 'localhost login:', vm=temp_vm)
        exec_command_and_wait_for_pattern(self, 'root', 'Password:', vm=temp_vm)
        exec_command_and_wait_for_pattern(self, self.root_password, self.prompt,
                                          vm=temp_vm)

        self._create_certificate(temp_vm)
        self._sign_binaries(temp_vm)
        self._run_zipl_secure(temp_vm)
        self._extract_certificate(temp_vm)

        # Shutdown temp vm
        temp_vm.shutdown()

    @skipBigDataTest()
    def test_s390x_secure_ipl(self):
        """
        Verify secure boot validation during s390x guest boot.

        Expects two "Verified component" messages and confirms
        /sys/firmware/ipl/secure reports secure boot is active.
        """
        self.require_accelerator('kvm')
        self.setup_s390x_secure_ipl()

        self.set_machine('s390-ccw-virtio')

        self.vm.set_console()
        self.vm.add_args('-nographic',
                         '-machine', 's390-ccw-virtio,secure-boot=on,'
                         f'boot-certs.0.path={self.cert_path}',
                         '-accel', 'kvm',
                         '-m', '1024',
                         '-drive',
                         f'id=drive1,if=none,format=qcow2,file={self.qcow2_path}',
                         '-device', 'virtio-blk-ccw,drive=drive1,bootindex=1')
        self.vm.launch()

        # Expect two verified components
        verified_output = "Verified component"
        wait_for_console_pattern(self, verified_output)
        wait_for_console_pattern(self, verified_output)

        # Login and verify the vm is booted using secure boot
        wait_for_console_pattern(self, 'localhost login:')
        exec_command_and_wait_for_pattern(self, 'root', 'Password:')
        exec_command_and_wait_for_pattern(self, self.root_password, self.prompt)
        exec_command_and_wait_for_pattern(self, 'cat /sys/firmware/ipl/secure', '1')

if __name__ == '__main__':
    QemuSystemTest.main()
