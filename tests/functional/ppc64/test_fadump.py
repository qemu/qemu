#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later

from unittest import skip
from qemu_test import Asset
from qemu_test import wait_for_console_pattern
from qemu_test import LinuxKernelTest
from qemu_test import exec_command, exec_command_and_wait_for_pattern

class QEMUFadump(LinuxKernelTest):
    """
    Functional test to verify Fadump is working in following scenarios:

    1. test_fadump_pseries:       PSeries
    2. test_fadump_pseries_kvm:   PSeries + KVM
    """

    timeout = 90
    KERNEL_COMMON_COMMAND_LINE = 'console=hvc0 fadump=on '
    msg_panic = 'Kernel panic - not syncing'
    msg_not_supported = 'Firmware-Assisted Dump is not supported on this hardware'
    msg_registered_success = ''
    msg_registered_failed = ''
    msg_dump_active = ''

    ASSET_EPAPR_KERNEL = Asset(
        ('https://github.com/open-power/op-build/releases/download/v2.7/'
         'zImage.epapr'),
        '0ab237df661727e5392cee97460e8674057a883c5f74381a128fa772588d45cd')

    ASSET_VMLINUZ_KERNEL = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora-secondary/'
         'releases/39/Everything/ppc64le/os/ppc/ppc64/vmlinuz'),
        ('81e5541d243b50c8f9568906c6918dda22239744d637bb9a7b22d23c3d661226'
         '8d5302beb2ca5c06f93bdbc9736c414ef5120756c8bf496ff488ad07d116d67f')
        )

    ASSET_FEDORA_INITRD = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora-secondary/'
        'releases/39/Everything/ppc64le/os/ppc/ppc64/initrd.img'),
        'e7f24b44cb2aaa67d30e551db6ac8d29cc57c934b158dabca6b7f885f2cfdd9b')

    def do_test_fadump(self, is_kvm=False, is_powernv=False):
        """
        Helper Function for Fadump tests below

        It boots the VM with fadump enabled, checks if fadump is correctly
        registered.
        Then crashes the system causing a QEMU_SYSTEM_RESET, after which
        dump should be available in the kernel.
        Finally it checks the filesize of the exported /proc/vmcore in 2nd
        kernel to verify it's same as the VM's memory size
        """
        if is_kvm:
            self.require_accelerator("kvm")
            self.vm.add_args("-accel", "kvm")
        else:
            self.require_accelerator("tcg")

        if is_powernv:
            self.set_machine("powernv10")
        else:
            # SLOF takes upto >20s in startup time, use VOF
            self.set_machine("pseries")
            self.vm.add_args("-machine", "x-vof=on")
            self.vm.add_args("-m", "6G")

        self.vm.set_console()

        kernel_path = None

        if is_powernv:
            kernel_path = self.ASSET_EPAPR_KERNEL.fetch()
        else:
            kernel_path = self.ASSET_VMLINUZ_KERNEL.fetch()

        initrd_path = self.ASSET_FEDORA_INITRD.fetch()

        self.vm.add_args('-kernel', kernel_path)
        self.vm.add_args('-initrd', initrd_path)
        self.vm.add_args('-append', "fadump=on"\
                         " -nodefaults -serial mon:stdio crashkernel=2G"\
                         " rdinit=/bin/sh ")

        self.vm.launch()

        # If kernel detects fadump support, and "fadump=on" is in command
        # line which we add above, it will print something like:
        #
        #     fadump: Reserved 1024MB of memory at 0x00000040000000 ...
        #
        # Else, if the kernel doesn't detect fadump support, it prints:
        #
        #     fadump: Firmware-Assisted Dump is not supported on this hardware
        #
        # Timeout after 20s if kernel doesn't print any fadump logs, this
        # can happen due to fadump being disabled in the kernel
        self.wait_for_regex_console_pattern(
            success_pattern="fadump: Reserved ",
            failure_pattern=r"fadump: (Firmware-Assisted Dump is not"\
            " supported on this hardware|Failed to find memory chunk for"\
            " reservation!)",
            timeout=20
        )

        # Ensure fadump is registered successfully, if registration
        # succeeds, we get a log from rtas fadump:
        #
        #     rtas fadump: Registration is successful!
        self.wait_for_console_pattern(
            "rtas fadump: Registration is successful!"
        )

        # Wait for the shell
        self.wait_for_console_pattern("#")

        # Mount /proc since not available in the initrd used
        exec_command(self, command="mount -t proc proc /proc")

        # Crash the kernel
        exec_command(self, command="echo c > /proc/sysrq-trigger")

        # Check for the kernel panic message, setting timeout to 20s as it
        # should occur almost immediately after previous echo c
        self.wait_for_regex_console_pattern(
            success_pattern="Kernel panic - not syncing: sysrq" \
                " triggered crash",
            timeout=20
        )

        # Check if fadump is active
        # If the kernel shows that fadump is active, that implies it's a
        # crashkernel boot
        # Else if the kernel shows "fadump: Reserved ..." then it's
        # treating this as the first kernel boot, this is likely the case
        # that qemu didn't pass the 'ibm,kernel-dump' device tree node
        wait_for_console_pattern(
            test=self,
            success_message="rtas fadump: Firmware-assisted dump is active",
            failure_message="fadump: Reserved "
        )

        # In a successful fadump boot, we get these logs:
        #
        # [    0.000000] fadump: Firmware-assisted dump is active.
        # [    0.000000] fadump: Reserving <>MB of memory at <> for preserving crash data
        #
        # Check if these logs are present in the fadump boot
        self.wait_for_console_pattern("preserving crash data")

        # Wait for prompt
        self.wait_for_console_pattern("sh-5.2#")

        # Mount /proc since not available in the initrd used
        exec_command_and_wait_for_pattern(self,
            command="mount -t proc proc /proc",
            success_message="#"
        )

        # Check if vmcore exists
        exec_command_and_wait_for_pattern(self,
            command="stat /proc/vmcore",
            success_message="File: /proc/vmcore",
            failure_message="No such file or directory"
        )

    def test_fadump_pseries(self):
        return self.do_test_fadump(is_kvm=False, is_powernv=False)

    @skip("PowerNV Fadump not supported yet")
    def test_fadump_powernv(self):
        return

    def test_fadump_pseries_kvm(self):
        """
        Test Fadump in PSeries with KVM accel
        """
        self.do_test_fadump(is_kvm=True, is_powernv=False)

if __name__ == '__main__':
    QEMUFadump.main()
