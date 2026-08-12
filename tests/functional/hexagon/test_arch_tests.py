#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import QemuSystemTest, Asset
from qemu_test.cmd import wait_for_console_pattern


class ArchTestsUart(QemuSystemTest):
    """
    Hexagon architecture verification tests

    These are bare-metal tests from hexagon-arch-tests that exercise
    system functionality.

    Tests output results via UART.
    """
    timeout = 60

    ASSET_TARBALL = Asset(
        "https://github.com/qualcomm/qemu-hexagon-testing/releases/"
        "download/v0.2.5/arch_tests_uart.tar.gz",
        "edb4f37b877a3a72a0e10920477458a43b40045d34398fee8cb763fefd342f4f",
    )

    def run_uart_test(self, test_name: str,
                      machine: str = "virt") -> None:
        """
        Run an arch test binary and verify PASS via UART console output.
        """
        self.set_machine(machine)
        self.archive_extract(self.ASSET_TARBALL)
        target_bin = self.scratch_file('arch_tests_uart_package',
                                      'bin', test_name)
        self.vm.set_console()
        self.set_vm_arg("-display", "none")
        self.set_vm_arg("-kernel", target_bin)
        self.vm.launch()
        wait_for_console_pattern(self, "PASS")

    def test_exceptions(self) -> None:
        """Tests exception delivery for trap instructions, privilege
        violations, and verifies SSR cause codes and ELR values.
        """
        self.run_uart_test("test_exceptions")

    def test_guest_mode(self) -> None:
        """Tests guest mode entry/exit via CCR configuration, verifying
        GSR fields, GELR, and guest event vector table dispatch.
        """
        self.run_uart_test("test_guest_mode")

    def test_int_steering(self) -> None:
        """Tests interrupt steering via priority-based routing to
        specific threads using STID priority and iassignw.
        """
        self.run_uart_test("test_int_steering")

    def test_cache(self) -> None:
        """Tests cache operations: dckill/ickill, l2kill, dczeroa,
        dccleaninva, cache disable/enable, barriers, and dcinva/dccleana.
        """
        self.run_uart_test("test_cache")

    def test_l2vic(self) -> None:
        """Tests the L2VIC interrupt controller: enable readback,
        interrupt type readback, VID capture, and the fast interface.
        """
        self.run_uart_test("test_l2vic")

    def test_threads(self) -> None:
        """Tests hardware thread management: start/stop, MODECTL state,
        per-thread HTID, shared memory, wait/resume, STID priority, and
        SCHEDCFG/BESTWAIT readback.
        """
        self.run_uart_test("test_threads")

    def test_tlb_mmu(self) -> None:
        """Tests TLB/MMU operations: write/read/probe/invalidate,
        global entries, multiple entries, overwrite, ASID matching,
        and permission checks.
        """
        self.run_uart_test("test_tlb_mmu")

    def test_user_mode(self) -> None:
        """Tests user mode / privilege transitions: supervisor mode,
        SSR UM/IE/XE/CE/PE bits, and the trap0 user-mode exit handler.
        """
        self.run_uart_test("test_user_mode")


if __name__ == "__main__":
    QemuSystemTest.main()
