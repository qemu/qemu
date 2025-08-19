#!/usr/bin/env python3
#
# virtio-balloon tests
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import time

from qemu_test import QemuSystemTest, Asset
from qemu_test import wait_for_console_pattern
from qemu_test import exec_command_and_wait_for_pattern

UNSET_STATS_VALUE = 18446744073709551615


class VirtioBalloonx86(QemuSystemTest):

    ASSET_KERNEL = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/releases'
         '/31/Server/x86_64/os/images/pxeboot/vmlinuz'),
        'd4738d03dbbe083ca610d0821d0a8f1488bebbdccef54ce33e3adb35fda00129')

    ASSET_INITRD = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/releases'
         '/31/Server/x86_64/os/images/pxeboot/initrd.img'),
        '277cd6c7adf77c7e63d73bbb2cded8ef9e2d3a2f100000e92ff1f8396513cd8b')

    ASSET_DISKIMAGE = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/releases'
         '/31/Cloud/x86_64/images/Fedora-Cloud-Base-31-1.9.x86_64.qcow2'),
        'e3c1b309d9203604922d6e255c2c5d098a309c2d46215d8fc026954f3c5c27a0')

    DEFAULT_KERNEL_PARAMS = ('root=/dev/vda1 console=ttyS0 net.ifnames=0 '
                             'rd.rescue quiet')

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(
            self,
            success_message,
            failure_message="Kernel panic - not syncing",
            vm=vm,
        )

    def mount_root(self):
        self.wait_for_console_pattern('Entering emergency mode.')
        prompt = '# '
        self.wait_for_console_pattern(prompt)

        # Synchronize on virtio-block driver creating the root device
        exec_command_and_wait_for_pattern(self,
                        "while ! (dmesg -c | grep vda:) ; do sleep 1 ; done",
                        "vda1")

        exec_command_and_wait_for_pattern(self, 'mount /dev/vda1 /sysroot',
                                          prompt)
        exec_command_and_wait_for_pattern(self, 'chroot /sysroot',
                                          prompt)
        exec_command_and_wait_for_pattern(self, "modprobe virtio-balloon",
                                          prompt)

    def assert_initial_stats(self):
        ret = self.vm.qmp('qom-get',
                          {'path': '/machine/peripheral/balloon',
                           'property': 'guest-stats'})['return']
        when = ret.get('last-update')
        assert when == 0
        stats = ret.get('stats')
        for name, val in stats.items():
            assert val == UNSET_STATS_VALUE

    def assert_running_stats(self, then):
        # We told the QEMU to refresh stats every 100ms, but
        # there can be a delay between virtio-ballon driver
        # being modprobed and seeing the first stats refresh
        # Retry a few times for robustness under heavy load
        retries = 10
        when = 0
        while when == 0 and retries:
            ret = self.vm.qmp('qom-get',
                              {'path': '/machine/peripheral/balloon',
                               'property': 'guest-stats'})['return']
            when = ret.get('last-update')
            if when == 0:
                retries = retries - 1
                time.sleep(0.5)

        now = time.time()

        assert when > then and when < now
        stats = ret.get('stats')
        # Stat we expect this particular Kernel to have set
        expectData = [
            "stat-available-memory",
            "stat-disk-caches",
            "stat-free-memory",
            "stat-htlb-pgalloc",
            "stat-htlb-pgfail",
            "stat-major-faults",
            "stat-minor-faults",
            "stat-swap-in",
            "stat-swap-out",
            "stat-total-memory",
        ]
        for name, val in stats.items():
            if name in expectData:
                assert val != UNSET_STATS_VALUE
            else:
                assert val == UNSET_STATS_VALUE

    def test_virtio_balloon_stats(self):
        self.set_machine('q35')
        self.require_accelerator("kvm")
        kernel_path = self.ASSET_KERNEL.fetch()
        initrd_path = self.ASSET_INITRD.fetch()
        diskimage_path = self.ASSET_DISKIMAGE.fetch()

        self.vm.set_console()
        self.vm.add_args("-S")
        self.vm.add_args("-cpu", "max")
        self.vm.add_args("-m", "2G")
        # Slow down BIOS phase with boot menu, so that after a system
        # reset, we can reliably catch the clean stats again in BIOS
        # phase before the guest OS launches
        self.vm.add_args("-boot", "menu=on")
        self.vm.add_args("-accel", "kvm")
        self.vm.add_args("-device", "virtio-balloon,id=balloon")
        self.vm.add_args('-drive',
                         f'file={diskimage_path},if=none,id=drv0,snapshot=on')
        self.vm.add_args('-device', 'virtio-blk-pci,bus=pcie.0,' +
                         'drive=drv0,id=virtio-disk0,bootindex=1')

        self.vm.add_args(
            "-kernel",
            kernel_path,
            "-initrd",
            initrd_path,
            "-append",
            self.DEFAULT_KERNEL_PARAMS
        )
        self.vm.launch()

        # Poll stats at 100ms
        self.vm.qmp('qom-set',
                    {'path': '/machine/peripheral/balloon',
                     'property': 'guest-stats-polling-interval',
                     'value': 100 })

        # We've not run any guest code yet, neither BIOS or guest,
        # so stats should be all default values
        self.assert_initial_stats()

        self.vm.qmp('cont')

        then = time.time()
        self.mount_root()
        self.assert_running_stats(then)

        # Race window between these two commands, where we
        # rely on '-boot menu=on' to (hopefully) ensure we're
        # still executing the BIOS when QEMU processes the
        # 'stop', and thus have not loaded the virtio-balloon
        # driver in the guest
        self.vm.qmp('system_reset')
        self.vm.qmp('stop')

        # If the above assumption held, we're in BIOS now and
        # stats should be all back at their default values
        self.assert_initial_stats()
        self.vm.qmp('cont')

        then = time.time()
        self.mount_root()
        self.assert_running_stats(then)


if __name__ == '__main__':
    QemuSystemTest.main()
