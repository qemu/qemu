"""
Check compatibility of virtio device types
"""
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Eduardo Habkost <ehabkost@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.
import sys
import os

from qemu.machine import QEMUMachine
from avocado_qemu import QemuSystemTest

# Virtio Device IDs:
VIRTIO_NET = 1
VIRTIO_BLOCK = 2
VIRTIO_CONSOLE = 3
VIRTIO_RNG = 4
VIRTIO_BALLOON = 5
VIRTIO_RPMSG = 7
VIRTIO_SCSI = 8
VIRTIO_9P = 9
VIRTIO_RPROC_SERIAL = 11
VIRTIO_CAIF = 12
VIRTIO_GPU = 16
VIRTIO_INPUT = 18
VIRTIO_VSOCK = 19
VIRTIO_CRYPTO = 20

PCI_VENDOR_ID_REDHAT_QUMRANET = 0x1af4

# Device IDs for legacy/transitional devices:
PCI_LEGACY_DEVICE_IDS = {
    VIRTIO_NET:     0x1000,
    VIRTIO_BLOCK:   0x1001,
    VIRTIO_BALLOON: 0x1002,
    VIRTIO_CONSOLE: 0x1003,
    VIRTIO_SCSI:    0x1004,
    VIRTIO_RNG:     0x1005,
    VIRTIO_9P:      0x1009,
    VIRTIO_VSOCK:   0x1012,
}

def pci_modern_device_id(virtio_devid):
    return virtio_devid + 0x1040

def devtype_implements(vm, devtype, implements):
    return devtype in [d['name'] for d in vm.command('qom-list-types', implements=implements)]

def get_pci_interfaces(vm, devtype):
    interfaces = ('pci-express-device', 'conventional-pci-device')
    return [i for i in interfaces if devtype_implements(vm, devtype, i)]

class VirtioVersionCheck(QemuSystemTest):
    """
    Check if virtio-version-specific device types result in the
    same device tree created by `disable-modern` and
    `disable-legacy`.

    :avocado: tags=arch:x86_64
    """

    # just in case there are failures, show larger diff:
    maxDiff = 4096

    def run_device(self, devtype, opts=None, machine='pc'):
        """
        Run QEMU with `-device DEVTYPE`, return device info from `query-pci`
        """
        with QEMUMachine(self.qemu_bin) as vm:
            vm.set_machine(machine)
            if opts:
                devtype += ',' + opts
            vm.add_args('-device', '%s,id=devfortest' % (devtype))
            vm.add_args('-S')
            vm.launch()

            pcibuses = vm.command('query-pci')
            alldevs = [dev for bus in pcibuses for dev in bus['devices']]
            devfortest = [dev for dev in alldevs
                          if dev['qdev_id'] == 'devfortest']
            return devfortest[0], get_pci_interfaces(vm, devtype)


    def assert_devids(self, dev, devid, non_transitional=False):
        self.assertEqual(dev['id']['vendor'], PCI_VENDOR_ID_REDHAT_QUMRANET)
        self.assertEqual(dev['id']['device'], devid)
        if non_transitional:
            self.assertTrue(0x1040 <= dev['id']['device'] <= 0x107f)
            self.assertGreaterEqual(dev['id']['subsystem'], 0x40)

    def check_all_variants(self, qemu_devtype, virtio_devid):
        """Check if a virtio device type and its variants behave as expected"""
        # Force modern mode:
        dev_modern, _ = self.run_device(qemu_devtype,
                                       'disable-modern=off,disable-legacy=on')
        self.assert_devids(dev_modern, pci_modern_device_id(virtio_devid),
                           non_transitional=True)

        # <prefix>-non-transitional device types should be 100% equivalent to
        # <prefix>,disable-modern=off,disable-legacy=on
        dev_1_0, nt_ifaces = self.run_device('%s-non-transitional' % (qemu_devtype))
        self.assertEqual(dev_modern, dev_1_0)

        # Force transitional mode:
        dev_trans, _ = self.run_device(qemu_devtype,
                                      'disable-modern=off,disable-legacy=off')
        self.assert_devids(dev_trans, PCI_LEGACY_DEVICE_IDS[virtio_devid])

        # Force legacy mode:
        dev_legacy, _ = self.run_device(qemu_devtype,
                                       'disable-modern=on,disable-legacy=off')
        self.assert_devids(dev_legacy, PCI_LEGACY_DEVICE_IDS[virtio_devid])

        # No options: default to transitional on PC machine-type:
        no_opts_pc, generic_ifaces = self.run_device(qemu_devtype)
        self.assertEqual(dev_trans, no_opts_pc)

        #TODO: check if plugging on a PCI Express bus will make the
        #      device non-transitional
        #no_opts_q35 = self.run_device(qemu_devtype, machine='q35')
        #self.assertEqual(dev_modern, no_opts_q35)

        # <prefix>-transitional device types should be 100% equivalent to
        # <prefix>,disable-modern=off,disable-legacy=off
        dev_trans, trans_ifaces = self.run_device('%s-transitional' % (qemu_devtype))
        self.assertEqual(dev_trans, dev_trans)

        # ensure the interface information is correct:
        self.assertIn('conventional-pci-device', generic_ifaces)
        self.assertIn('pci-express-device', generic_ifaces)

        self.assertIn('conventional-pci-device', nt_ifaces)
        self.assertIn('pci-express-device', nt_ifaces)

        self.assertIn('conventional-pci-device', trans_ifaces)
        self.assertNotIn('pci-express-device', trans_ifaces)


    def test_conventional_devs(self):
        self.check_all_variants('virtio-net-pci', VIRTIO_NET)
        # virtio-blk requires 'driver' parameter
        #self.check_all_variants('virtio-blk-pci', VIRTIO_BLOCK)
        self.check_all_variants('virtio-serial-pci', VIRTIO_CONSOLE)
        self.check_all_variants('virtio-rng-pci', VIRTIO_RNG)
        self.check_all_variants('virtio-balloon-pci', VIRTIO_BALLOON)
        self.check_all_variants('virtio-scsi-pci', VIRTIO_SCSI)
        # virtio-9p requires 'fsdev' parameter
        #self.check_all_variants('virtio-9p-pci', VIRTIO_9P)

    def check_modern_only(self, qemu_devtype, virtio_devid):
        """Check if a modern-only virtio device type behaves as expected"""
        # Force modern mode:
        dev_modern, _ = self.run_device(qemu_devtype,
                                       'disable-modern=off,disable-legacy=on')
        self.assert_devids(dev_modern, pci_modern_device_id(virtio_devid),
                           non_transitional=True)

        # No options: should be modern anyway
        dev_no_opts, ifaces = self.run_device(qemu_devtype)
        self.assertEqual(dev_modern, dev_no_opts)

        self.assertIn('conventional-pci-device', ifaces)
        self.assertIn('pci-express-device', ifaces)

    def test_modern_only_devs(self):
        self.check_modern_only('virtio-vga', VIRTIO_GPU)
        self.check_modern_only('virtio-gpu-pci', VIRTIO_GPU)
        self.check_modern_only('virtio-mouse-pci', VIRTIO_INPUT)
        self.check_modern_only('virtio-tablet-pci', VIRTIO_INPUT)
        self.check_modern_only('virtio-keyboard-pci', VIRTIO_INPUT)
