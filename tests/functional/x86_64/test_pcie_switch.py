#!/usr/bin/env python3
#
# Functional test for generic PCIe switch upstream and downstream ports
#
# Copyright (C) 2026 Nutanix, Inc.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset, exec_command_and_wait_for_pattern


class PCIeSwitchPort(LinuxKernelTest):

    timeout = 120

    ASSET_KERNEL = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/releases'
         '/31/Server/x86_64/os/images/pxeboot/vmlinuz'),
        'd4738d03dbbe083ca610d0821d0a8f1488bebbdccef54ce33e3adb35fda00129')

    ASSET_INITRD = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/releases'
         '/31/Server/x86_64/os/images/pxeboot/initrd.img'),
        '277cd6c7adf77c7e63d73bbb2cded8ef9e2d3a2f100000e92ff1f8396513cd8b')

    USP_VENDOR_ID = '0x1b36'
    USP_DEVICE_ID = '0x0015'
    DSP_VENDOR_ID = '0x1b36'
    DSP_DEVICE_ID = '0x0016'
    CLASS_BRIDGE_PCI_PCI = '0x060400'

    def command(self, command: str, expected_result: int = 0):
        command = f'echo __begin ; {command} ; D="done" ; echo "$?/__$D"'
        output = exec_command_and_wait_for_pattern(self, command, '__done')
        lines = output.decode().splitlines()
        for s, line in enumerate(lines):
            if line == '__begin':
                break
        result = int(lines[-1].split("/")[0])
        assert result == expected_result
        return "\n".join(lines[s+1:-1])

    def test_pcie_switch_ports(self):
        """Boot a q35 VM with a PCIe switch (USP + DSP) and verify topology."""
        self.require_accelerator('kvm')
        self.set_machine('q35')

        self.vm.add_args('-accel', 'kvm')
        self.vm.add_args('-m', '1G')
        self.vm.add_args('-device',
                         'pcie-root-port,id=rp0,slot=0,chassis=0,bus=pcie.0')
        self.vm.add_args('-device',
                         'pcie-upstream-port,id=sw-usp,bus=rp0')
        self.vm.add_args('-device',
                         'pcie-downstream-port,id=sw-dsp,bus=sw-usp,'
                         'chassis=1')
        self.vm.add_args('-append', 'console=ttyS0 rd.rescue')

        self.launch_kernel(self.ASSET_KERNEL.fetch(),
                           self.ASSET_INITRD.fetch(),
                           wait_for='Entering emergency mode.')
        self.wait_for_console_pattern('# ')

        # Capture lspci output, to diagnose failures.
        #
        # [0000:00]-+-00.0  8086:29c0
        #           +-01.0  8086:10d3
        #           +-02.0-[01-03]----00.0-[02-03]----00.0-[03]--
        #           +-1f.0  8086:2918
        #           +-1f.2  8086:2922
        #           \-1f.3  8086:2930
        self.command("lspci -vtn")
        self.command("lspci -vvv")
        self.command("ls -R /sys/devices/")

        usp_path = "/sys/devices/pci0000:00/0000:00:02.0/0000:01:00.0"
        dsp_path = usp_path + "/0000:02:00.0"

        # Upstream port is expected model.
        vendor_id = self.command(f'cat {usp_path}/vendor')
        assert vendor_id == self.USP_VENDOR_ID

        device_id = self.command(f'cat {usp_path}/device')
        assert device_id == self.USP_DEVICE_ID

        class_code = self.command(f'cat {usp_path}/class')
        assert class_code == self.CLASS_BRIDGE_PCI_PCI

        # Downstream port is expected model.
        vendor_id = self.command(f'cat {dsp_path}/vendor')
        assert vendor_id == self.DSP_VENDOR_ID

        device_id = self.command(f'cat {dsp_path}/device')
        assert device_id == self.DSP_DEVICE_ID


if __name__ == '__main__':
    LinuxKernelTest.main()
