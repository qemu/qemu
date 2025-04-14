#!/usr/bin/env python3
#
# Tests that specifically try to exercise hypervisor features of the
# target machines. powernv supports the Power hypervisor ISA, and
# pseries supports the nested-HV hypervisor spec.
#
# Copyright (c) 2023 IBM Corporation
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import subprocess

from datetime import datetime
from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern, exec_command
from qemu_test import skipIfMissingCommands, skipBigDataTest
from qemu_test import exec_command_and_wait_for_pattern

# Alpine is a light weight distro that supports QEMU. These tests boot
# that on the machine then run a QEMU guest inside it in KVM mode,
# that runs the same Alpine distro image.
# QEMU packages are downloaded and installed on each test. That's not a
# large download, but it may be more polite to create qcow2 image with
# QEMU already installed and use that.
# XXX: The order of these tests seems to matter, see git blame.
@skipIfMissingCommands("xorriso")
@skipBigDataTest()
class HypervisorTest(QemuSystemTest):

    timeout = 1000
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 console=hvc0 '
    panic_message = 'Kernel panic - not syncing'
    good_message = 'VFS: Cannot open root device'

    ASSET_ISO = Asset(
        ('https://dl-cdn.alpinelinux.org/alpine/v3.21/'
         'releases/ppc64le/alpine-standard-3.21.0-ppc64le.iso'),
        '7651ab4e3027604535c0b36e86c901b4695bf8fe97b908f5b48590f6baae8f30')

    def extract_from_iso(self, iso, path):
        """
        Extracts a file from an iso file into the test workdir

        :param iso: path to the iso file
        :param path: path within the iso file of the file to be extracted
        :returns: path of the extracted file
        """
        filename = self.scratch_file(os.path.basename(path))

        cmd = "xorriso -osirrox on -indev %s -cpx %s %s" % (iso, path, filename)
        subprocess.run(cmd.split(),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        os.chmod(filename, 0o600)

        return filename

    def setUp(self):
        super().setUp()

        self.iso_path = self.ASSET_ISO.fetch()
        self.vmlinuz = self.extract_from_iso(self.iso_path, '/boot/vmlinuz-lts')
        self.initramfs = self.extract_from_iso(self.iso_path, '/boot/initramfs-lts')

    def do_start_alpine(self):
        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE
        self.vm.add_args("-kernel", self.vmlinuz)
        self.vm.add_args("-initrd", self.initramfs)
        self.vm.add_args("-smp", "4", "-m", "2g")
        self.vm.add_args("-drive", f"file={self.iso_path},format=raw,if=none,"
                                    "id=drive0,read-only=true")

        self.vm.launch()
        ps1='localhost:~#'
        wait_for_console_pattern(self, 'localhost login:')
        exec_command_and_wait_for_pattern(self, 'root', ps1)
        # If the time is wrong, SSL certificates can fail.
        exec_command_and_wait_for_pattern(self, 'date -s "' + datetime.utcnow().strftime('%Y-%m-%d %H:%M:%S' + '"'), ps1)
        ps1='alpine:~#'
        exec_command_and_wait_for_pattern(self, 'setup-alpine -qe', ps1)
        exec_command_and_wait_for_pattern(self, 'setup-apkrepos -c1', ps1)
        exec_command_and_wait_for_pattern(self, 'apk update', ps1)
        # Could upgrade here but it usually should not be necessary
        # exec_command_and_wait_for_pattern(self, 'apk upgrade --available', ps1)

    def do_stop_alpine(self):
        exec_command(self, 'echo "TEST ME"')
        wait_for_console_pattern(self, 'alpine:~#')
        exec_command(self, 'poweroff')
        wait_for_console_pattern(self, 'reboot: Power down')
        self.vm.wait()

    def do_setup_kvm(self):
        ps1='alpine:~#'
        exec_command_and_wait_for_pattern(self, 'apk add qemu-system-ppc64', ps1)
        exec_command_and_wait_for_pattern(self, 'modprobe kvm-hv', ps1)

    # This uses the host's block device as the source file for guest block
    # device for install media. This is a bit hacky but allows reuse of the
    # iso without having a passthrough filesystem configured.
    def do_test_kvm(self, hpt=False):
        if hpt:
            append = 'disable_radix'
        else:
            append = ''
        exec_command(self, 'qemu-system-ppc64 -nographic -smp 2 -m 1g '
                           '-machine pseries,x-vof=on,accel=kvm '
                           '-machine cap-cfpc=broken,cap-sbbc=broken,'
                                    'cap-ibs=broken,cap-ccf-assist=off '
                           '-drive file=/dev/nvme0n1,format=raw,readonly=on '
                           '-initrd /media/nvme0n1/boot/initramfs-lts '
                           '-kernel /media/nvme0n1/boot/vmlinuz-lts '
                           '-append \'usbcore.nousb ' + append + '\'')
        # Alpine 3.21 kernel seems to crash in XHCI USB driver.
        ps1='localhost:~#'
        wait_for_console_pattern(self, 'localhost login:')
        exec_command_and_wait_for_pattern(self, 'root', ps1)
        exec_command(self, 'poweroff')
        wait_for_console_pattern(self, 'reboot: Power down')
        # Now wait for the host's prompt to come back
        wait_for_console_pattern(self, 'alpine:~#')

    def test_hv_pseries(self):
        self.require_accelerator("tcg")
        self.require_netdev('user')
        self.set_machine('pseries')
        self.vm.add_args("-accel", "tcg,thread=multi")
        self.vm.add_args('-device', 'nvme,serial=1234,drive=drive0')
        self.vm.add_args("-machine", "x-vof=on,cap-nested-hv=on")
        self.do_start_alpine()
        self.do_setup_kvm()
        self.do_test_kvm()
        self.do_stop_alpine()

    def test_hv_pseries_kvm(self):
        self.require_accelerator("kvm")
        self.require_netdev('user')
        self.set_machine('pseries')
        self.vm.add_args("-accel", "kvm")
        self.vm.add_args('-device', 'nvme,serial=1234,drive=drive0')
        self.vm.add_args("-machine", "x-vof=on,cap-nested-hv=on,cap-ccf-assist=off")
        self.do_start_alpine()
        self.do_setup_kvm()
        self.do_test_kvm()
        self.do_stop_alpine()

    def test_hv_powernv(self):
        self.require_accelerator("tcg")
        self.require_netdev('user')
        self.set_machine('powernv')
        self.vm.add_args("-accel", "tcg,thread=multi")
        self.vm.add_args('-device', 'nvme,bus=pcie.2,addr=0x0,serial=1234,drive=drive0',
                         '-device', 'e1000e,netdev=net0,mac=C0:FF:EE:00:00:02,bus=pcie.0,addr=0x0',
                         '-netdev', 'user,id=net0,hostfwd=::20022-:22,hostname=alpine')
        self.do_start_alpine()
        self.do_setup_kvm()
        self.do_test_kvm()
        self.do_test_kvm(True)
        self.do_stop_alpine()

if __name__ == '__main__':
    QemuSystemTest.main()
