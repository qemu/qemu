#!/usr/bin/env python3
#
# Test that Linux kernel boots on ppc powernv machines and check the console
#
# Copyright (c) 2018, 2020 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from qemu_test import LinuxKernelTest, Asset
from qemu_test import wait_for_console_pattern
from qemu_test import exec_command_and_wait_for_pattern

class PowernvMachine(LinuxKernelTest):

    timeout = 90
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 console=hvc0 '
    panic_message = 'Kernel panic - not syncing'
    good_message = 'VFS: Cannot open root device'

    ASSET_KERNEL = Asset(
        ('https://github.com/legoater/qemu-ppc-boot/raw/refs/heads/main/'
         'buildroot/qemu_ppc64le_powernv8-2025.02/vmlinux'),
        '6fd29aff9ad4362511ea5d0acbb510667c7031928e97d64ec15bbc5daf4b8151')

    ASSET_INITRD = Asset(
        ('https://github.com/legoater/qemu-ppc-boot/raw/refs/heads/main/'
         'buildroot/qemu_ppc64le_powernv8-2025.02/rootfs.ext2'),
        'aee2192b692077c4bde31cb56ce474424b358f17cec323d5c94af3970c9aada2')

    # testdtb for power11, which contains string "hello world" in command line
    ASSET_SAMPLE_DTB = Asset(
        ('https://github.com/roz3x/qemu/raw/refs/heads/sample-dtb/qemu-powernv11.dtb'),
        'ea1271516264eea1eb58a067a99d0c2ca9528be8dc7d4e46bb2d5ae0d42fc568')

    def shell_exec_command_check_fail(self, command):
        fail_msg="Fail"
        self.shell_exec_command(f"export __FAIL_MSG={fail_msg}")

        # If the exit code for the command is non 0, print fail message
        command = command + " || echo $__FAIL_MSG"
        exec_command_and_wait_for_pattern(self, command, '#', fail_msg)

    def shell_exec_command(self, command):
        exec_command_and_wait_for_pattern(self, command, '#', self.panic_message)

    def do_test_linux_boot(self, command_line = KERNEL_COMMON_COMMAND_LINE):
        self.require_accelerator("tcg")
        kernel_path = self.ASSET_KERNEL.fetch()

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-append', command_line)
        self.vm.launch()

    def test_linux_boot(self):
        self.set_machine('powernv')
        self.do_test_linux_boot()
        console_pattern = 'VFS: Cannot open root device'
        wait_for_console_pattern(self, console_pattern, self.panic_message)

    def test_linux_smp_boot(self):
        self.set_machine('powernv')
        self.vm.add_args('-smp', '4')
        self.do_test_linux_boot()
        console_pattern = 'smp: Brought up 1 node, 4 CPUs'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)

    def test_linux_smp_hpt_boot(self):
        self.set_machine('powernv')
        self.vm.add_args('-smp', '4')
        self.do_test_linux_boot(self.KERNEL_COMMON_COMMAND_LINE +
                                'disable_radix')
        console_pattern = 'smp: Brought up 1 node, 4 CPUs'
        wait_for_console_pattern(self, 'hash-mmu: Initializing hash mmu',
                                 self.panic_message)
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)

    def test_linux_smt_boot(self):
        self.set_machine('powernv')
        self.vm.add_args('-smp', '4,threads=4')
        self.do_test_linux_boot()
        console_pattern = 'CPU maps initialized for 4 threads per core'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        console_pattern = 'smp: Brought up 1 node, 4 CPUs'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)

    def test_linux_remote_interrupts(self):
        self.require_accelerator("tcg")
        self.require_netdev('user')
        self.set_machine('powernv')

        # Have below setup in this test:
        # 1. e1000e attached to pcie.6, which is from 7th PHB, belonging to 2nd
        #    socket (chip 1), in a powernv boot with default 6 PHBs per socket
        # 2. CPU on 2nd socket (chip 1) disabled
        # 3. RX IRQ's affinity to chip 2, and TX IRQ's affinity to chip 3
        #
        # Then ping is done, to generate interrupts from e1000e which should go
        # to IRQ server on the remote sockets
        self.vm.add_args('-smp', '4,sockets=4,cores=1,threads=1')
        self.vm.add_args('-netdev', 'user,id=net0')
        self.vm.add_args('-device', 'e1000e,netdev=net0,bus=pcie.6')

        kernel_path = self.ASSET_KERNEL.fetch()
        rootfs_path = self.ASSET_INITRD.fetch()
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
            '-drive',
            f'file={rootfs_path},format=raw,if=none,id=drive0,readonly=on',
            '-append', 'root=/dev/nvme0n1 console=hvc0',
            '-device', 'nvme,drive=drive0,bus=pcie.2,addr=0x0,serial=1234')
        self.vm.launch()

        # Wait for boot to complete
        console_pattern = 'CPU maps initialized for 1 thread per core'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        console_pattern = 'smp: Brought up 4 nodes, 4 CPUs'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, 'Run /sbin/init as init process',
                                 self.panic_message)

        # Wait for login prompt and login as root (no password in buildroot)
        wait_for_console_pattern(self, 'login:', self.panic_message)
        exec_command_and_wait_for_pattern(self, 'root', '#', self.panic_message)

        # e1000e is connected to socket 1, disable the CPU on socket 1
        self.shell_exec_command("echo 0 > /sys/devices/system/cpu/cpu1/online")
        self.shell_exec_command(
            "export CPU1_STATE=$(cat /sys/devices/system/cpu/cpu1/online)")
        self.shell_exec_command_check_fail("[ $CPU1_STATE -eq 0 ]")

        # RX, TX interrupts to chip/cpu 2 & 3 respectively
        self.shell_exec_command(
            "export RX_IRQ=$(awk '/eth0-rx/ {print $1}' /proc/interrupts | tr -d ':')")
        self.shell_exec_command(
            "export TX_IRQ=$(awk '/eth0-tx/ {print $1}' /proc/interrupts | tr -d ':')")
        self.shell_exec_command("echo 2 > /proc/irq/$RX_IRQ/smp_affinity_list")
        self.shell_exec_command("echo 3 > /proc/irq/$TX_IRQ/smp_affinity_list")

        # Capture interrupt counts before generating traffic
        self.shell_exec_command(
            "export RX_BEFORE=$(awk '/eth0-rx/ {print $3}' /proc/interrupts)")
        self.shell_exec_command(
            "export TX_BEFORE=$(awk '/eth0-tx/ {print $4}' /proc/interrupts)")

        # Wait up to 15 seconds for eth0 link to come up
        self.shell_exec_command(
            "c=0; while ! ip addr show eth0 | grep 'inet 10.0.2'; do "
            "sleep 1; c=$((c+1)); [ $c -gt 15 ] && break; done")

        self.shell_exec_command_check_fail(
            "ip addr show eth0 | grep 'inet 10.0.2'")

        # Generate network traffic to trigger remote interrupts
        # Ping QEMU's user-mode network gateway (10.0.2.2)
        self.shell_exec_command("ping -W2 -c5 10.0.2.2")

        # Show final interrupt counts to verify remote interrupts occurred
        self.shell_exec_command("cat /proc/interrupts | grep eth0")

        # Verify interrupt counts increased (whether interrupts were delivered)
        self.shell_exec_command(
            "export RX_AFTER=$(awk '/eth0-rx/ {print $3}' /proc/interrupts)")
        self.shell_exec_command(
            "export TX_AFTER=$(awk '/eth0-tx/ {print $4}' /proc/interrupts)")

        # Check that interrupt counts increased
        self.shell_exec_command_check_fail("[ $RX_AFTER -gt $RX_BEFORE ]")
        self.shell_exec_command_check_fail("[ $TX_AFTER -gt $TX_BEFORE ]")

    def test_linux_big_boot(self):
        self.set_machine('powernv')
        self.vm.add_args('-smp', '16,threads=4,cores=2,sockets=2')

        # powernv does not support NUMA
        self.do_test_linux_boot()
        console_pattern = 'CPU maps initialized for 4 threads per core'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        console_pattern = 'smp: Brought up 2 nodes, 16 CPUs'
        wait_for_console_pattern(self, console_pattern, self.panic_message)
        wait_for_console_pattern(self, self.good_message, self.panic_message)

    def do_test_ppc64_powernv(self, proc):
        self.require_accelerator("tcg")
        kernel_path = self.ASSET_KERNEL.fetch()
        initrd_path = self.ASSET_INITRD.fetch()
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-drive',
                         f'file={initrd_path},format=raw,if=none,id=drive0,readonly=on',
                         '-append', 'root=/dev/nvme0n1 console=tty0 console=hvc0',
                         '-device', 'pcie-pci-bridge,id=bridge1,bus=pcie.1,addr=0x0',
                         '-device', 'nvme,drive=drive0,bus=pcie.2,addr=0x0,serial=1234',
                         '-device', 'e1000e,bus=bridge1,addr=0x3',
                         '-device', 'nec-usb-xhci,bus=bridge1,addr=0x2')
        self.vm.launch()

        self.wait_for_console_pattern("CPU: " + proc + " generation processor")
        self.wait_for_console_pattern("INIT: Starting kernel at ")
        self.wait_for_console_pattern("Run /sbin/init as init process")
        # Device detection output driven by udev probing is sometimes cut off
        # from console output, suspect S14silence-console init script.

    def test_ppc64_powernv_external_dtb(self):
        self.set_machine('powernv11')
        self.require_accelerator("tcg")

        kernel_path = self.ASSET_KERNEL.fetch()
        sample_dtb_path = self.ASSET_SAMPLE_DTB.fetch()
        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', sample_dtb_path)
        self.vm.launch()

        # check if custom dtb is reflected or not
        wait_for_console_pattern(self, "Kernel command line: hello world", self.panic_message)

    def test_powernv8(self):
        self.set_machine('powernv8')
        self.do_test_ppc64_powernv('P8')

    def test_powernv9(self):
        self.set_machine('powernv9')
        self.do_test_ppc64_powernv('P9')

    def test_powernv10(self):
        self.set_machine('powernv10')
        self.do_test_ppc64_powernv('P10')

    def test_powernv11(self):
        self.set_machine('powernv11')
        self.do_test_ppc64_powernv('Power11')

if __name__ == '__main__':
    LinuxKernelTest.main()
