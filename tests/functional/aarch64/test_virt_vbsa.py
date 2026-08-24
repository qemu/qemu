#!/usr/bin/env python3
#
# Functional test that runs the Arm VBSA conformance tests.
#
# Copyright (c) 2026 Linaro Ltd.
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

import shutil
import re
from subprocess import check_call, check_output, DEVNULL

from qemu_test import QemuSystemTest, Asset
from qemu_test import get_qemu_img, skipIfMissingCommands
from qemu_test import wait_for_console_pattern
from qemu_test import exec_command_and_wait_for_pattern as ec_and_wait
from qemu.machine.machine import VMLaunchFailure


@skipIfMissingCommands("mformat", "mcopy", "mmd")
class Aarch64VirtMachine(QemuSystemTest):
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '
    timeout = 360

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='FAILED',
                                 vm=vm)

    def append_firmware_blobs(self):
        """
        Setup QEMU firmware blobs for boot.
        """
        code_path = self.build_file('pc-bios', 'edk2-aarch64-code.fd')
        vars_source = self.build_file('pc-bios', 'edk2-aarch64-vars.fd')
        vars_path = self.scratch_file('vars.fd')
        shutil.copy(vars_source, vars_path)

        self.vm.add_args('-drive',
                         f'if=pflash,format=raw,readonly=on,file={code_path}')
        self.vm.add_args('-drive', f'if=pflash,format=raw,file={vars_path}')


    ASSET_VBSA_EFI = Asset(
        'https://github.com/ARM-software/sysarch-acs/raw/refs/heads/main'
        '/prebuilt_images/VBSA/v25.12_VBSA_0.7.0/Vbsa.efi',
        '80f37d2fb86d152d95dec4d05ff099c9e47ee8a89314268e08056b0e1359e1fa')

    ASSET_BSA_SHELL = Asset(
        'https://github.com/ARM-software/sysarch-acs/raw/refs/heads/main'
        '/prebuilt_images/VBSA/v25.12_VBSA_0.7.0/Shell.efi',
        'e526604f0d329b481c6a1f62f7a0db8ea24ce8178b2c6abda8e247425f38775c')

    def test_aarch64_vbsa_uefi_tests(self):
        """
        Launch the UEFI based VBSA test from an EFI file-system
        """

        self.vm.set_console()

        # virt machine
        self.set_machine('virt')
        self.vm.add_args('-M', 'virt,gic-version=max,virtualization=on')
        self.vm.add_args('-cpu', 'max', '-m', '1024')

        self.append_firmware_blobs()

        # Build an EFI FAT32 file-system for the UEFI tests
        vbsa_efi = self.ASSET_VBSA_EFI.fetch()
        bsa_shell = self.ASSET_BSA_SHELL.fetch()

        img_path = self.scratch_file('vbsa.img')
        qemu_img = get_qemu_img(self)
        check_call([qemu_img, 'create', '-f', 'raw', img_path, '64M'],
                   stdout=DEVNULL, stderr=DEVNULL)

        check_call(['mformat', '-i', img_path, '-v', 'VBSA', '::'],
                   stdout=DEVNULL, stderr=DEVNULL)

        check_call(['mmd', '-i', img_path, '::/EFI'],
                   stdout=DEVNULL, stderr=DEVNULL)

        check_call(['mmd', '-i', img_path, '::/EFI/BOOT'],
                   stdout=DEVNULL, stderr=DEVNULL)

        check_call(['mcopy', '-i', img_path, bsa_shell,
                    '::/EFI/BOOT/BOOTAA64.EFI'],
                   stdout=DEVNULL, stderr=DEVNULL)

        check_call(['mcopy', '-i', img_path, vbsa_efi, '::/Vbsa.efi'],
                   stdout=DEVNULL, stderr=DEVNULL)

        self.vm.add_args('-drive',
                         f'file={img_path},format=raw,if=none,id=drive0')
        self.vm.add_args('-device', 'virtio-blk-pci,drive=drive0')

        try:
            self.vm.launch()
        except VMLaunchFailure as excp:
            if "does not support providing Virtualization" in excp.output:
                self.skipTest("accelerator has no virtualization support")
            else:
                self.log.info("unhandled launch failure: %s", excp.output)
                raise excp

        # wait for EFI prompt
        self.wait_for_console_pattern('Shell>')

        # Start the VBSA tests
        ec_and_wait(self, "FS0:Vbsa.efi", 'VBSA Architecture Compliance Suite')

        # could we parse the summary somehow?

        self.wait_for_console_pattern('VBSA tests complete. Reset the system.')


    ASSET_SYSREADY_IMAGE = Asset(
        'https://github.com/ARM-software/arm-systemready/'
        'releases/download/v25.10_SR_3.1.0/systemready_acs_live_image.img.xz.zip',
        'df2c359de15784b1da6a8e6f3c98a053ee38ac0b3f241ccea62e17db092eb03a')

    ROOT_PROMPT = '/ # '

    @skipIfMissingCommands("sfdisk", "jq", "sed")
    def test_aarch64_vbsa_linux_tests(self):
        """
        Launch the Linux based VBSA test from the ACS prebuilt images.

        We can use the pre-built images and then trigger the Linux
        build and run the tests. We then need to slurp the results
        from the partition.
        """

        self.vm.set_console()

        # Plain virt machine
        self.set_machine('virt')
        self.vm.add_args('-M', 'virt,gic-version=max')
        self.vm.add_args('-cpu', 'max', '-m', '1024', '-smp', '4')

        self.append_firmware_blobs()

        # First fetch, decompress (twice) and prepare the disk image
        # on an NVME device (the kernel only has drivers for that).
        self.archive_extract(self.ASSET_SYSREADY_IMAGE, format="zip")
        disk_image_xz = self.scratch_file("systemready_acs_live_image.img.xz")
        disk_image = self.uncompress(disk_image_xz)

        self.vm.add_args('-device',
                         'nvme,drive=hd,serial=QEMU_ROOT_SSD')
        self.vm.add_args('-blockdev',
                         f'driver=raw,node-name=hd,file.driver=file,file.filename={disk_image}')

        # Tweak grub.cfg default to avoid manually navigating grub
        grub_cfg = self.scratch_file("grub.cfg")
        offset = int(check_output(f"sfdisk --json {disk_image} |"
                                  "jq '.partitiontable.partitions[0].start * 512'",
                                  shell=True))
        check_call(["mcopy", "-i", f"{disk_image}@@{offset}",
                    "::/EFI/BOOT/grub.cfg", grub_cfg])

        with open(grub_cfg, 'a', encoding="utf8") as f:
            f.write("set default='Linux Execution Enviroment'")

        check_call(["mcopy", "-D", "o",  "-i", f"{disk_image}@@{offset}",
                    grub_cfg, "::/EFI/BOOT/grub.cfg"])

        # Launch QEMU and wait for grub and select the "Linux
        # Execution Environment" so we can launch the test.

        self.vm.launch()
        self.wait_for_console_pattern(self.ROOT_PROMPT)
        ec_and_wait(self, "/usr/bin/bsa.sh --skip "
                    "B_REP_1,B_IEP_1,B_PCIe_11,B_MEM_06",
                    self.ROOT_PROMPT)

        # Now we can shutdown
        ec_and_wait(self, "halt -f", "reboot: System halted")
        self.vm.shutdown()

        # and extract the test logs
        bsa_app_res = self.scratch_file("BsaResultsApp.log")
        bsa_kern_res = self.scratch_file("BsaResultsKernel.log")

        check_call(["mcopy", "-i", f"{disk_image}@@{offset}",
                    "::acs_results/Linux/BsaResultsApp.log", bsa_app_res])
        check_call(["mcopy", "-i", f"{disk_image}@@{offset}",
                    "::acs_results/Linux/BsaResultsKernel.log", bsa_kern_res])

        # for now just check the kernel log for the result summary
        test_result_re = re.compile(r"\[.*\]\s+(.+): Result:\s+(\w+)")
        summary_re = re.compile(r"Total Tests Run =\s*(\d+), Tests Passed =\s*(\d+), Tests Failed =\s*(\d+)")

        with open(bsa_kern_res, 'r', encoding="utf8") as f:
            for line in f:
                test_match = test_result_re.search(line)
                if test_match:
                    desc = test_match.group(1)
                    status = test_match.group(2)
                    self.log.info(f"Test: {desc} status: {status}")

                match = summary_re.search(line)
                if match:
                    total, passed, failed = match.groups()

                    if int(failed) > 0:
                        self.fail(f"{failed} tests failed ({total})")


if __name__ == '__main__':
    QemuSystemTest.main()
