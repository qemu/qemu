#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
'''
Functional test that checks the pxelinux.cfg network booting of a s390x VM
(TFTP booting without config file is already tested by the pxe qtest, so
we don't repeat that here).
'''

import os
import shutil

from qemu_test import QemuSystemTest, Asset, wait_for_console_pattern


PXELINUX_CFG_CONTENTS='''# pxelinux.cfg style config file
default Debian
label Nonexisting
kernel kernel.notavailable
initrd initrd.notavailable
label Debian
kernel kernel.debian
initrd initrd.debian
append testoption=teststring
label Fedora
kernel kernel.fedora
'''

class S390PxeLinux(QemuSystemTest):
    '''
    Test various ways of booting via a pxelinux.cfg file, for details see:
    https://wiki.syslinux.org/wiki/index.php?title=PXELINUX#Configuration
    '''

    ASSET_DEBIAN_KERNEL = Asset(
        ('https://snapshot.debian.org/archive/debian/'
         '20201126T092837Z/dists/buster/main/installer-s390x/'
         '20190702+deb10u6/images/generic/kernel.debian'),
        'd411d17c39ae7ad38d27534376cbe88b68b403c325739364122c2e6f1537e818')

    ASSET_DEBIAN_INITRD = Asset(
        ('https://snapshot.debian.org/archive/debian/'
         '20201126T092837Z/dists/buster/main/installer-s390x/'
         '20190702+deb10u6/images/generic/initrd.debian'),
        '836bbd0fe6a5ca81274c28c2b063ea315ce1868660866e9b60180c575fef9fd5')

    ASSET_FEDORA_KERNEL = Asset(
        ('https://archives.fedoraproject.org/pub/archive'
         '/fedora-secondary/releases/31/Server/s390x/os'
         '/images/kernel.img'),
        '480859574f3f44caa6cd35c62d70e1ac0609134e22ce2a954bbed9b110c06e0b')

    def pxelinux_launch(self, pl_name='default', extra_opts=None):
        '''Create a pxelinux.cfg file in the right location and launch QEMU'''
        self.require_netdev('user')
        self.set_machine('s390-ccw-virtio')

        debian_kernel = self.ASSET_DEBIAN_KERNEL.fetch()
        debian_initrd = self.ASSET_DEBIAN_INITRD.fetch()
        fedora_kernel = self.ASSET_FEDORA_KERNEL.fetch()

        # Prepare a folder for the TFTP "server":
        tftpdir = self.scratch_file('tftp')
        shutil.rmtree(tftpdir, ignore_errors=True)   # Remove stale stuff
        os.mkdir(tftpdir)
        shutil.copy(debian_kernel, os.path.join(tftpdir, 'kernel.debian'))
        shutil.copy(debian_initrd, os.path.join(tftpdir, 'initrd.debian'))
        shutil.copy(fedora_kernel, os.path.join(tftpdir, 'kernel.fedora'))

        pxelinuxdir = self.scratch_file('tftp', 'pxelinux.cfg')
        os.mkdir(pxelinuxdir)

        cfg_fname = self.scratch_file('tftp', 'pxelinux.cfg', pl_name)
        with open(cfg_fname, 'w', encoding='utf-8') as f:
            f.write(PXELINUX_CFG_CONTENTS)

        virtio_net_dev = 'virtio-net-ccw,netdev=n1,bootindex=1'
        if extra_opts:
            virtio_net_dev += ',' + extra_opts

        self.vm.add_args('-m', '384',
                         '-netdev', f'user,id=n1,tftp={tftpdir}',
                         '-device', virtio_net_dev)
        self.vm.set_console()
        self.vm.launch()


    def test_default(self):
        '''Check whether the guest uses the "default" file name'''
        self.pxelinux_launch()
        # The kernel prints its arguments to the console, so we can use
        # this to check whether the kernel parameters are correctly handled:
        wait_for_console_pattern(self, 'testoption=teststring')
        # Now also check that we've successfully loaded the initrd:
        wait_for_console_pattern(self, 'Unpacking initramfs...')
        wait_for_console_pattern(self, 'Run /init as init process')

    def test_mac(self):
        '''Check whether the guest uses file name based on its MAC address'''
        self.pxelinux_launch(pl_name='01-02-ca-fe-ba-be-42',
                             extra_opts='mac=02:ca:fe:ba:be:42,loadparm=3')
        wait_for_console_pattern(self, 'Linux version 5.3.7-301.fc31.s390x')

    def test_uuid(self):
        '''Check whether the guest uses file name based on its UUID'''
        # Also add a non-bootable disk to check the fallback to network boot:
        self.vm.add_args('-blockdev', 'null-co,size=65536,node-name=d1',
                         '-device', 'virtio-blk,drive=d1,bootindex=0,loadparm=1',
                         '-uuid', '550e8400-e29b-11d4-a716-446655441234')
        self.pxelinux_launch(pl_name='550e8400-e29b-11d4-a716-446655441234')
        wait_for_console_pattern(self, 'Debian 4.19.146-1 (2020-09-17)')

    def test_ip(self):
        '''Check whether the guest uses file name based on its IP address'''
        self.vm.add_args('-M', 'loadparm=3')
        self.pxelinux_launch(pl_name='0A00020F')
        wait_for_console_pattern(self, 'Linux version 5.3.7-301.fc31.s390x')

    def test_menu(self):
        '''Check whether the boot menu works for pxelinux.cfg booting'''
        self.vm.add_args('-boot', 'menu=on,splash-time=10')
        self.pxelinux_launch(pl_name='0A00')
        wait_for_console_pattern(self, '[1] Nonexisting')
        wait_for_console_pattern(self, '[2] Debian')
        wait_for_console_pattern(self, '[3] Fedora')
        wait_for_console_pattern(self, 'Debian 4.19.146-1 (2020-09-17)')


if __name__ == '__main__':
    QemuSystemTest.main()
