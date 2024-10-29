# Functional test that boots a Linux kernel and checks the console
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import lzma
import gzip
import shutil

from avocado import skip
from avocado import skipUnless
from avocado import skipUnless
from avocado_qemu import QemuSystemTest
from avocado_qemu import exec_command
from avocado_qemu import exec_command_and_wait_for_pattern
from avocado_qemu import interrupt_interactive_console_until_pattern
from avocado_qemu import wait_for_console_pattern
from avocado.utils import process
from avocado.utils import archive

"""
Round up to next power of 2
"""
def pow2ceil(x):
    return 1 if x == 0 else 2**(x - 1).bit_length()

def file_truncate(path, size):
    if size != os.path.getsize(path):
        with open(path, 'ab+') as fd:
            fd.truncate(size)

"""
Expand file size to next power of 2
"""
def image_pow2ceil_expand(path):
        size = os.path.getsize(path)
        size_aligned = pow2ceil(size)
        if size != size_aligned:
            with open(path, 'ab+') as fd:
                fd.truncate(size_aligned)

class LinuxKernelTest(QemuSystemTest):
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    def extract_from_deb(self, deb, path):
        """
        Extracts a file from a deb package into the test workdir

        :param deb: path to the deb archive
        :param path: path within the deb archive of the file to be extracted
        :returns: path of the extracted file
        """
        cwd = os.getcwd()
        os.chdir(self.workdir)
        file_path = process.run("ar t %s" % deb).stdout_text.split()[2]
        process.run("ar x %s %s" % (deb, file_path))
        archive.extract(file_path, self.workdir)
        os.chdir(cwd)
        # Return complete path to extracted file.  Because callers to
        # extract_from_deb() specify 'path' with a leading slash, it is
        # necessary to use os.path.relpath() as otherwise os.path.join()
        # interprets it as an absolute path and drops the self.workdir part.
        return os.path.normpath(os.path.join(self.workdir,
                                             os.path.relpath(path, '/')))

    def extract_from_rpm(self, rpm, path):
        """
        Extracts a file from an RPM package into the test workdir.

        :param rpm: path to the rpm archive
        :param path: path within the rpm archive of the file to be extracted
                     needs to be a relative path (starting with './') because
                     cpio(1), which is used to extract the file, expects that.
        :returns: path of the extracted file
        """
        cwd = os.getcwd()
        os.chdir(self.workdir)
        process.run("rpm2cpio %s | cpio -id %s" % (rpm, path), shell=True)
        os.chdir(cwd)
        return os.path.normpath(os.path.join(self.workdir, path))

class BootLinuxConsole(LinuxKernelTest):
    """
    Boots a Linux kernel and checks that the console is operational and the
    kernel command line is properly passed from QEMU to the kernel
    """
    timeout = 90

    def test_x86_64_pc(self):
        """
        :avocado: tags=arch:x86_64
        :avocado: tags=machine:pc
        """
        kernel_url = ('https://archives.fedoraproject.org/pub/archive/fedora'
                      '/linux/releases/29/Everything/x86_64/os/images/pxeboot'
                      '/vmlinuz')
        kernel_hash = '23bebd2680757891cf7adedb033532163a792495'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    def test_aarch64_xlnx_versal_virt(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=machine:xlnx-versal-virt
        :avocado: tags=device:pl011
        :avocado: tags=device:arm_gicv3
        :avocado: tags=accel:tcg
        """
        images_url = ('http://ports.ubuntu.com/ubuntu-ports/dists/'
                      'bionic-updates/main/installer-arm64/'
                      '20101020ubuntu543.19/images/')
        kernel_url = images_url + 'netboot/ubuntu-installer/arm64/linux'
        kernel_hash = 'e167757620640eb26de0972f578741924abb3a82'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        initrd_url = images_url + 'netboot/ubuntu-installer/arm64/initrd.gz'
        initrd_hash = 'cab5cb3fcefca8408aa5aae57f24574bfce8bdb9'
        initrd_path = self.fetch_asset(initrd_url, asset_hash=initrd_hash)

        self.vm.set_console()
        self.vm.add_args('-m', '2G',
                         '-accel', 'tcg',
                         '-kernel', kernel_path,
                         '-initrd', initrd_path)
        self.vm.launch()
        self.wait_for_console_pattern('Checked W+X mappings: passed')

    def test_arm_virt(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:virt
        :avocado: tags=accel:tcg
        """
        kernel_url = ('https://archives.fedoraproject.org/pub/archive/fedora'
                      '/linux/releases/29/Everything/armhfp/os/images/pxeboot'
                      '/vmlinuz')
        kernel_hash = 'e9826d741b4fb04cadba8d4824d1ed3b7fb8b4d4'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0')
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    def test_arm_emcraft_sf2(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:emcraft-sf2
        :avocado: tags=endian:little
        :avocado: tags=u-boot
        :avocado: tags=accel:tcg
        """
        self.require_netdev('user')

        uboot_url = ('https://raw.githubusercontent.com/'
                     'Subbaraya-Sundeep/qemu-test-binaries/'
                     'fe371d32e50ca682391e1e70ab98c2942aeffb01/u-boot')
        uboot_hash = 'cbb8cbab970f594bf6523b9855be209c08374ae2'
        uboot_path = self.fetch_asset(uboot_url, asset_hash=uboot_hash)
        spi_url = ('https://raw.githubusercontent.com/'
                   'Subbaraya-Sundeep/qemu-test-binaries/'
                   'fe371d32e50ca682391e1e70ab98c2942aeffb01/spi.bin')
        spi_hash = '65523a1835949b6f4553be96dec1b6a38fb05501'
        spi_path = self.fetch_asset(spi_url, asset_hash=spi_hash)
        spi_path_rw = os.path.join(self.workdir, os.path.basename(spi_path))
        shutil.copy(spi_path, spi_path_rw)

        file_truncate(spi_path_rw, 16 << 20) # Spansion S25FL128SDPBHICO is 16 MiB

        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE
        self.vm.add_args('-kernel', uboot_path,
                         '-append', kernel_command_line,
                         '-drive', 'file=' + spi_path_rw + ',if=mtd,format=raw',
                         '-no-reboot')
        self.vm.launch()
        self.wait_for_console_pattern('Enter \'help\' for a list')

        exec_command_and_wait_for_pattern(self, 'ifconfig eth0 10.0.2.15',
                                                 'eth0: link becomes ready')
        exec_command_and_wait_for_pattern(self, 'ping -c 3 10.0.2.2',
            '3 packets transmitted, 3 packets received, 0% packet loss')

    def test_arm_exynos4210_initrd(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:smdkc210
        :avocado: tags=accel:tcg
        """
        deb_url = ('https://snapshot.debian.org/archive/debian/'
                   '20190928T224601Z/pool/main/l/linux/'
                   'linux-image-4.19.0-6-armmp_4.19.67-2+deb10u1_armhf.deb')
        deb_hash = 'fa9df4a0d38936cb50084838f2cb933f570d7d82'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinuz-4.19.0-6-armmp')
        dtb_path = '/usr/lib/linux-image-4.19.0-6-armmp/exynos4210-smdkv310.dtb'
        dtb_path = self.extract_from_deb(deb_path, dtb_path)

        initrd_url = ('https://github.com/groeck/linux-build-test/raw/'
                      '2eb0a73b5d5a28df3170c546ddaaa9757e1e0848/rootfs/'
                      'arm/rootfs-armv5.cpio.gz')
        initrd_hash = '2b50f1873e113523967806f4da2afe385462ff9b'
        initrd_path_gz = self.fetch_asset(initrd_url, asset_hash=initrd_hash)
        initrd_path = os.path.join(self.workdir, 'rootfs.cpio')
        archive.gzip_uncompress(initrd_path_gz, initrd_path)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'earlycon=exynos4210,0x13800000 earlyprintk ' +
                               'console=ttySAC0,115200n8 ' +
                               'random.trust_cpu=off cryptomgr.notests ' +
                               'cpuidle.off=1 panic=-1 noreboot')

        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line,
                         '-no-reboot')
        self.vm.launch()

        self.wait_for_console_pattern('Boot successful.')
        # TODO user command, for now the uart is stuck

    def test_arm_cubieboard_initrd(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:cubieboard
        :avocado: tags=accel:tcg
        """
        deb_url = ('https://apt.armbian.com/pool/main/l/'
                   'linux-6.6.16/linux-image-current-sunxi_24.2.1_armhf__6.6.16-Seb3e-D6b4a-P2359-Ce96bHfe66-HK01ba-V014b-B067e-R448a.deb')
        deb_hash = 'f7c3c8c5432f765445dc6e7eab02f3bbe668256b'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinuz-6.6.16-current-sunxi')
        dtb_path = '/usr/lib/linux-image-6.6.16-current-sunxi/sun4i-a10-cubieboard.dtb'
        dtb_path = self.extract_from_deb(deb_path, dtb_path)
        initrd_url = ('https://github.com/groeck/linux-build-test/raw/'
                      '2eb0a73b5d5a28df3170c546ddaaa9757e1e0848/rootfs/'
                      'arm/rootfs-armv5.cpio.gz')
        initrd_hash = '2b50f1873e113523967806f4da2afe385462ff9b'
        initrd_path_gz = self.fetch_asset(initrd_url, asset_hash=initrd_hash)
        initrd_path = os.path.join(self.workdir, 'rootfs.cpio')
        archive.gzip_uncompress(initrd_path_gz, initrd_path)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200 '
                               'usbcore.nousb '
                               'panic=-1 noreboot')
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line,
                         '-no-reboot')
        self.vm.launch()
        self.wait_for_console_pattern('Boot successful.')

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'Allwinner sun4i/sun5i')
        exec_command_and_wait_for_pattern(self, 'cat /proc/iomem',
                                                'system-control@1c00000')
        exec_command_and_wait_for_pattern(self, 'reboot',
                                                'reboot: Restarting system')
        # Wait for VM to shut down gracefully
        self.vm.wait()

    def test_arm_cubieboard_sata(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:cubieboard
        :avocado: tags=accel:tcg
        """
        deb_url = ('https://apt.armbian.com/pool/main/l/'
                   'linux-6.6.16/linux-image-current-sunxi_24.2.1_armhf__6.6.16-Seb3e-D6b4a-P2359-Ce96bHfe66-HK01ba-V014b-B067e-R448a.deb')
        deb_hash = 'f7c3c8c5432f765445dc6e7eab02f3bbe668256b'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinuz-6.6.16-current-sunxi')
        dtb_path = '/usr/lib/linux-image-6.6.16-current-sunxi/sun4i-a10-cubieboard.dtb'
        dtb_path = self.extract_from_deb(deb_path, dtb_path)
        rootfs_url = ('https://github.com/groeck/linux-build-test/raw/'
                      '2eb0a73b5d5a28df3170c546ddaaa9757e1e0848/rootfs/'
                      'arm/rootfs-armv5.ext2.gz')
        rootfs_hash = '093e89d2b4d982234bf528bc9fb2f2f17a9d1f93'
        rootfs_path_gz = self.fetch_asset(rootfs_url, asset_hash=rootfs_hash)
        rootfs_path = os.path.join(self.workdir, 'rootfs.cpio')
        archive.gzip_uncompress(rootfs_path_gz, rootfs_path)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200 '
                               'usbcore.nousb '
                               'root=/dev/sda ro '
                               'panic=-1 noreboot')
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-drive', 'if=none,format=raw,id=disk0,file='
                                   + rootfs_path,
                         '-device', 'ide-hd,bus=ide.0,drive=disk0',
                         '-append', kernel_command_line,
                         '-no-reboot')
        self.vm.launch()
        self.wait_for_console_pattern('Boot successful.')

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'Allwinner sun4i/sun5i')
        exec_command_and_wait_for_pattern(self, 'cat /proc/partitions',
                                                'sda')
        exec_command_and_wait_for_pattern(self, 'reboot',
                                                'reboot: Restarting system')
        # Wait for VM to shut down gracefully
        self.vm.wait()

    @skipUnless(os.getenv('AVOCADO_ALLOW_LARGE_STORAGE'), 'storage limited')
    def test_arm_cubieboard_openwrt_22_03_2(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:cubieboard
        :avocado: tags=device:sd
        """

        # This test download a 7.5 MiB compressed image and expand it
        # to 126 MiB.
        image_url = ('https://downloads.openwrt.org/releases/22.03.2/targets/'
                     'sunxi/cortexa8/openwrt-22.03.2-sunxi-cortexa8-'
                     'cubietech_a10-cubieboard-ext4-sdcard.img.gz')
        image_hash = ('94b5ecbfbc0b3b56276e5146b899eafa'
                      '2ac5dc2d08733d6705af9f144f39f554')
        image_path_gz = self.fetch_asset(image_url, asset_hash=image_hash,
                                         algorithm='sha256')
        image_path = archive.extract(image_path_gz, self.workdir)
        image_pow2ceil_expand(image_path)

        self.vm.set_console()
        self.vm.add_args('-drive', 'file=' + image_path + ',if=sd,format=raw',
                         '-nic', 'user',
                         '-no-reboot')
        self.vm.launch()

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'usbcore.nousb '
                               'noreboot')

        self.wait_for_console_pattern('U-Boot SPL')

        interrupt_interactive_console_until_pattern(
                self, 'Hit any key to stop autoboot:', '=>')
        exec_command_and_wait_for_pattern(self, "setenv extraargs '" +
                                                kernel_command_line + "'", '=>')
        exec_command_and_wait_for_pattern(self, 'boot', 'Starting kernel ...');

        self.wait_for_console_pattern(
            'Please press Enter to activate this console.')

        exec_command_and_wait_for_pattern(self, ' ', 'root@')

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'Allwinner sun4i/sun5i')
        exec_command_and_wait_for_pattern(self, 'reboot',
                                                'reboot: Restarting system')
        # Wait for VM to shut down gracefully
        self.vm.wait()

    @skipUnless(os.getenv('AVOCADO_TIMEOUT_EXPECTED'), 'Test might timeout')
    def test_arm_quanta_gsj(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:quanta-gsj
        :avocado: tags=accel:tcg
        """
        # 25 MiB compressed, 32 MiB uncompressed.
        image_url = (
                'https://github.com/hskinnemoen/openbmc/releases/download/'
                '20200711-gsj-qemu-0/obmc-phosphor-image-gsj.static.mtd.gz')
        image_hash = '14895e634923345cb5c8776037ff7876df96f6b1'
        image_path_gz = self.fetch_asset(image_url, asset_hash=image_hash)
        image_name = 'obmc.mtd'
        image_path = os.path.join(self.workdir, image_name)
        archive.gzip_uncompress(image_path_gz, image_path)

        self.vm.set_console()
        drive_args = 'file=' + image_path + ',if=mtd,bus=0,unit=0'
        self.vm.add_args('-drive', drive_args)
        self.vm.launch()

        # Disable drivers and services that stall for a long time during boot,
        # to avoid running past the 90-second timeout. These may be removed
        # as the corresponding device support is added.
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + (
                'console=${console} '
                'mem=${mem} '
                'initcall_blacklist=npcm_i2c_bus_driver_init '
                'systemd.mask=systemd-random-seed.service '
                'systemd.mask=dropbearkey.service '
        )

        self.wait_for_console_pattern('> BootBlock by Nuvoton')
        self.wait_for_console_pattern('>Device: Poleg BMC NPCM730')
        self.wait_for_console_pattern('>Skip DDR init.')
        self.wait_for_console_pattern('U-Boot ')
        interrupt_interactive_console_until_pattern(
                self, 'Hit any key to stop autoboot:', 'U-Boot>')
        exec_command_and_wait_for_pattern(
                self, "setenv bootargs ${bootargs} " + kernel_command_line,
                'U-Boot>')
        exec_command_and_wait_for_pattern(
                self, 'run romboot', 'Booting Kernel from flash')
        self.wait_for_console_pattern('Booting Linux on physical CPU 0x0')
        self.wait_for_console_pattern('CPU1: thread -1, cpu 1, socket 0')
        self.wait_for_console_pattern('OpenBMC Project Reference Distro')
        self.wait_for_console_pattern('gsj login:')

    def test_arm_quanta_gsj_initrd(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:quanta-gsj
        :avocado: tags=accel:tcg
        """
        initrd_url = (
                'https://github.com/hskinnemoen/openbmc/releases/download/'
                '20200711-gsj-qemu-0/obmc-phosphor-initramfs-gsj.cpio.xz')
        initrd_hash = '98fefe5d7e56727b1eb17d5c00311b1b5c945300'
        initrd_path = self.fetch_asset(initrd_url, asset_hash=initrd_hash)
        kernel_url = (
                'https://github.com/hskinnemoen/openbmc/releases/download/'
                '20200711-gsj-qemu-0/uImage-gsj.bin')
        kernel_hash = 'fa67b2f141d56d39b3c54305c0e8a899c99eb2c7'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)
        dtb_url = (
                'https://github.com/hskinnemoen/openbmc/releases/download/'
                '20200711-gsj-qemu-0/nuvoton-npcm730-gsj.dtb')
        dtb_hash = '18315f7006d7b688d8312d5c727eecd819aa36a4'
        dtb_path = self.fetch_asset(dtb_url, asset_hash=dtb_hash)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200n8 '
                               'earlycon=uart8250,mmio32,0xf0001000')
        self.vm.add_args('-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-dtb', dtb_path,
                         '-append', kernel_command_line)
        self.vm.launch()

        self.wait_for_console_pattern('Booting Linux on physical CPU 0x0')
        self.wait_for_console_pattern('CPU1: thread -1, cpu 1, socket 0')
        self.wait_for_console_pattern(
                'Give root password for system maintenance')

    def test_arm_ast2600_debian(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:rainier-bmc
        """
        deb_url = ('http://snapshot.debian.org/archive/debian/'
                   '20220606T211338Z/'
                   'pool/main/l/linux/'
                   'linux-image-5.17.0-2-armmp_5.17.6-1%2Bb1_armhf.deb')
        deb_hash = '8acb2b4439faedc2f3ed4bdb2847ad4f6e0491f73debaeb7f660c8abe4dcdc0e'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash,
                                    algorithm='sha256')
        kernel_path = self.extract_from_deb(deb_path, '/boot/vmlinuz-5.17.0-2-armmp')
        dtb_path = self.extract_from_deb(deb_path,
                '/usr/lib/linux-image-5.17.0-2-armmp/aspeed-bmc-ibm-rainier.dtb')

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-net', 'nic')
        self.vm.launch()
        self.wait_for_console_pattern("Booting Linux on physical CPU 0xf00")
        self.wait_for_console_pattern("SMP: Total of 2 processors activated")
        self.wait_for_console_pattern("No filesystem could mount root")

