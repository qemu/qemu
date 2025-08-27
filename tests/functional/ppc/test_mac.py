#!/usr/bin/env python3
#
# Boot Linux kernel on a mac99 and g3beige ppc machine and check the console
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu_test import LinuxKernelTest, Asset


class MacTest(LinuxKernelTest):

    ASSET_DAY15 = Asset(
        'https://qemu-advcal.gitlab.io/qac-best-of-multiarch/download/day15.tar.xz',
        '03e0757c131d2959decf293a3572d3b96c5a53587165bf05ce41b2818a2bccd5')

    def do_day15_test(self):
        # mac99 also works with kvm_pr but we don't have a reliable way at
        # the moment (e.g. by looking at /proc/modules) to detect whether
        # we're running kvm_hv or kvm_pr. For now let's disable this test
        # if we don't have TCG support.
        self.require_accelerator("tcg")
        self.archive_extract(self.ASSET_DAY15)
        self.vm.add_args('-M', 'graphics=off')
        self.launch_kernel(self.scratch_file('day15', 'invaders.elf'),
                           wait_for='QEMU advent calendar')

    def test_ppc_g3beige(self):
        self.set_machine('g3beige')
        self.do_day15_test()

    def test_ppc_mac99(self):
        self.set_machine('mac99')
        self.do_day15_test()

if __name__ == '__main__':
    LinuxKernelTest.main()
