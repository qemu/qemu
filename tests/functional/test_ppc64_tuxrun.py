#!/usr/bin/env python3
#
# Functional test that boots known good tuxboot images the same way
# that tuxrun (www.tuxrun.org) does. This tool is used by things like
# the LKFT project to run regression tests on kernels.
#
# Copyright (c) 2023 Linaro Ltd.
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from subprocess import check_call, DEVNULL
import tempfile

from qemu_test import Asset
from qemu_test.tuxruntest import TuxRunBaselineTest

class TuxRunPPC64Test(TuxRunBaselineTest):

    def ppc64_common_tuxrun(self, kernel_asset, rootfs_asset, prefix):
        self.set_machine('pseries')
        self.cpu='POWER10'
        self.console='hvc0'
        self.root='sda'
        self.extradev='spapr-vscsi'
        # add device args to command line.
        self.require_netdev('user')
        self.vm.add_args('-netdev', 'user,id=vnet,hostfwd=:127.0.0.1:0-:22',
                         '-device', 'virtio-net,netdev=vnet')
        self.vm.add_args('-netdev', '{"type":"user","id":"hostnet0"}',
                         '-device', '{"driver":"virtio-net-pci","netdev":'
                         '"hostnet0","id":"net0","mac":"52:54:00:4c:e3:86",'
                         '"bus":"pci.0","addr":"0x9"}')
        self.vm.add_args('-device', '{"driver":"qemu-xhci","p2":15,"p3":15,'
                         '"id":"usb","bus":"pci.0","addr":"0x2"}')
        self.vm.add_args('-device', '{"driver":"virtio-scsi-pci","id":"scsi0"'
                         ',"bus":"pci.0","addr":"0x3"}')
        self.vm.add_args('-device', '{"driver":"virtio-serial-pci","id":'
                         '"virtio-serial0","bus":"pci.0","addr":"0x4"}')
        self.vm.add_args('-device', '{"driver":"scsi-cd","bus":"scsi0.0"'
                         ',"channel":0,"scsi-id":0,"lun":0,"device_id":'
                         '"drive-scsi0-0-0-0","id":"scsi0-0-0-0"}')
        self.vm.add_args('-device', '{"driver":"virtio-balloon-pci",'
                         '"id":"balloon0","bus":"pci.0","addr":"0x6"}')
        self.vm.add_args('-audiodev', '{"id":"audio1","driver":"none"}')
        self.vm.add_args('-device', '{"driver":"usb-tablet","id":"input0"'
                         ',"bus":"usb.0","port":"1"}')
        self.vm.add_args('-device', '{"driver":"usb-kbd","id":"input1"'
                         ',"bus":"usb.0","port":"2"}')
        self.vm.add_args('-device', '{"driver":"VGA","id":"video0",'
                         '"vgamem_mb":16,"bus":"pci.0","addr":"0x7"}')
        self.vm.add_args('-object', '{"qom-type":"rng-random","id":"objrng0"'
                         ',"filename":"/dev/urandom"}',
                         '-device', '{"driver":"virtio-rng-pci","rng":"objrng0"'
                         ',"id":"rng0","bus":"pci.0","addr":"0x8"}')
        self.vm.add_args('-object', '{"qom-type":"cryptodev-backend-builtin",'
                         '"id":"objcrypto0","queues":1}',
                         '-device', '{"driver":"virtio-crypto-pci",'
                         '"cryptodev":"objcrypto0","id":"crypto0","bus"'
                         ':"pci.0","addr":"0xa"}')
        self.vm.add_args('-device', '{"driver":"spapr-pci-host-bridge"'
                         ',"index":1,"id":"pci.1"}')
        self.vm.add_args('-device', '{"driver":"spapr-vscsi","id":"scsi1"'
                         ',"reg":12288}')
        self.vm.add_args('-m', '1G,slots=32,maxmem=2G',
                         '-object', 'memory-backend-ram,id=ram1,size=1G',
                         '-device', 'pc-dimm,id=dimm1,memdev=ram1')

        # Create a temporary qcow2 and launch the test-case
        with tempfile.NamedTemporaryFile(prefix=prefix,
                                         suffix='.qcow2') as qcow2:
            check_call([self.qemu_img, 'create', '-f', 'qcow2',
                        qcow2.name, ' 1G'],
                       stdout=DEVNULL, stderr=DEVNULL)

            self.vm.add_args('-drive', 'file=' + qcow2.name +
                         ',format=qcow2,if=none,id='
                         'drive-virtio-disk1',
                         '-device', 'virtio-blk-pci,bus=pci.0,'
                         'addr=0xb,drive=drive-virtio-disk1,id=virtio-disk1'
                         ',bootindex=2')
            self.common_tuxrun(kernel_asset, rootfs_asset=rootfs_asset,
                               drive="scsi-hd")

    ASSET_PPC64_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/ppc64/vmlinux',
        '8219d5cb26e7654ad7826fe8aee6290f7c01eef44f2cd6d26c15fe8f99e1c17c')
    ASSET_PPC64_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/ppc64/rootfs.ext4.zst',
        'b68e12314303c5dd0fef37ae98021299a206085ae591893e73557af99a02d373')

    def test_ppc64(self):
        self.ppc64_common_tuxrun(kernel_asset=self.ASSET_PPC64_KERNEL,
                                 rootfs_asset=self.ASSET_PPC64_ROOTFS,
                                 prefix='tuxrun_ppc64_')

    ASSET_PPC64LE_KERNEL = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/ppc64le/vmlinux',
        '21aea1fbc18bf6fa7d8ca4ea48d4940b2c8363c077acd564eb47d769b7495279')
    ASSET_PPC64LE_ROOTFS = Asset(
        'https://storage.tuxboot.com/buildroot/20241119/ppc64le/rootfs.ext4.zst',
        '67d36a3f9597b738e8b7359bdf04500f4d9bb82fc35eaa65aa439d888b2392f4')

    def test_ppc64le(self):
        self.ppc64_common_tuxrun(kernel_asset=self.ASSET_PPC64LE_KERNEL,
                                 rootfs_asset=self.ASSET_PPC64LE_ROOTFS,
                                 prefix='tuxrun_ppc64le_')


if __name__ == '__main__':
    TuxRunBaselineTest.main()
