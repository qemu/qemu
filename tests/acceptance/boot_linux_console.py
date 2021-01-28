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
from avocado_qemu import Test
from avocado_qemu import exec_command_and_wait_for_pattern
from avocado_qemu import interrupt_interactive_console_until_pattern
from avocado_qemu import wait_for_console_pattern
from avocado.utils import process
from avocado.utils import archive
from avocado.utils.path import find_command, CmdNotFoundError

P7ZIP_AVAILABLE = True
try:
    find_command('7z')
except CmdNotFoundError:
    P7ZIP_AVAILABLE = False

"""
Round up to next power of 2
"""
def pow2ceil(x):
    return 1 if x == 0 else 2**(x - 1).bit_length()

"""
Expand file size to next power of 2
"""
def image_pow2ceil_expand(path):
        size = os.path.getsize(path)
        size_aligned = pow2ceil(size)
        if size != size_aligned:
            with open(path, 'ab+') as fd:
                fd.truncate(size_aligned)

class LinuxKernelTest(Test):
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

    def test_mips_malta(self):
        """
        :avocado: tags=arch:mips
        :avocado: tags=machine:malta
        :avocado: tags=endian:big
        """
        deb_url = ('http://snapshot.debian.org/archive/debian/'
                   '20130217T032700Z/pool/main/l/linux-2.6/'
                   'linux-image-2.6.32-5-4kc-malta_2.6.32-48_mips.deb')
        deb_hash = 'a8cfc28ad8f45f54811fc6cf74fc43ffcfe0ba04'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinux-2.6.32-5-4kc-malta')

        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    def test_mips64el_malta(self):
        """
        This test requires the ar tool to extract "data.tar.gz" from
        the Debian package.

        The kernel can be rebuilt using this Debian kernel source [1] and
        following the instructions on [2].

        [1] http://snapshot.debian.org/package/linux-2.6/2.6.32-48/
            #linux-source-2.6.32_2.6.32-48
        [2] https://kernel-team.pages.debian.net/kernel-handbook/
            ch-common-tasks.html#s-common-official

        :avocado: tags=arch:mips64el
        :avocado: tags=machine:malta
        """
        deb_url = ('http://snapshot.debian.org/archive/debian/'
                   '20130217T032700Z/pool/main/l/linux-2.6/'
                   'linux-image-2.6.32-5-5kc-malta_2.6.32-48_mipsel.deb')
        deb_hash = '1aaec92083bf22fda31e0d27fa8d9a388e5fc3d5'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinux-2.6.32-5-5kc-malta')

        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    def test_mips64el_fuloong2e(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=machine:fuloong2e
        :avocado: tags=endian:little
        """
        deb_url = ('http://archive.debian.org/debian/pool/main/l/linux/'
                   'linux-image-3.16.0-6-loongson-2e_3.16.56-1+deb8u1_mipsel.deb')
        deb_hash = 'd04d446045deecf7b755ef576551de0c4184dd44'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinux-3.16.0-6-loongson-2e')

        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    def test_mips_malta_cpio(self):
        """
        :avocado: tags=arch:mips
        :avocado: tags=machine:malta
        :avocado: tags=endian:big
        """
        deb_url = ('http://snapshot.debian.org/archive/debian/'
                   '20160601T041800Z/pool/main/l/linux/'
                   'linux-image-4.5.0-2-4kc-malta_4.5.5-1_mips.deb')
        deb_hash = 'a3c84f3e88b54e06107d65a410d1d1e8e0f340f8'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinux-4.5.0-2-4kc-malta')
        initrd_url = ('https://github.com/groeck/linux-build-test/raw/'
                      '8584a59ed9e5eb5ee7ca91f6d74bbb06619205b8/rootfs/'
                      'mips/rootfs.cpio.gz')
        initrd_hash = 'bf806e17009360a866bf537f6de66590de349a99'
        initrd_path_gz = self.fetch_asset(initrd_url, asset_hash=initrd_hash)
        initrd_path = self.workdir + "rootfs.cpio"
        archive.gzip_uncompress(initrd_path_gz, initrd_path)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE
                               + 'console=ttyS0 console=tty '
                               + 'rdinit=/sbin/init noreboot')
        self.vm.add_args('-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line,
                         '-no-reboot')
        self.vm.launch()
        self.wait_for_console_pattern('Boot successful.')

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'BogoMIPS')
        exec_command_and_wait_for_pattern(self, 'uname -a',
                                                'Debian')
        exec_command_and_wait_for_pattern(self, 'reboot',
                                                'reboot: Restarting system')
        # Wait for VM to shut down gracefully
        self.vm.wait()

    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    def test_mips64el_malta_5KEc_cpio(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=machine:malta
        :avocado: tags=endian:little
        """
        kernel_url = ('https://github.com/philmd/qemu-testing-blob/'
                      'raw/9ad2df38/mips/malta/mips64el/'
                      'vmlinux-3.19.3.mtoman.20150408')
        kernel_hash = '00d1d268fb9f7d8beda1de6bebcc46e884d71754'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)
        initrd_url = ('https://github.com/groeck/linux-build-test/'
                      'raw/8584a59e/rootfs/'
                      'mipsel64/rootfs.mipsel64r1.cpio.gz')
        initrd_hash = '1dbb8a396e916847325284dbe2151167'
        initrd_path_gz = self.fetch_asset(initrd_url, algorithm='md5',
                                          asset_hash=initrd_hash)
        initrd_path = self.workdir + "rootfs.cpio"
        archive.gzip_uncompress(initrd_path_gz, initrd_path)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE
                               + 'console=ttyS0 console=tty '
                               + 'rdinit=/sbin/init noreboot')
        self.vm.add_args('-cpu', '5KEc',
                         '-kernel', kernel_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line,
                         '-no-reboot')
        self.vm.launch()
        wait_for_console_pattern(self, 'Boot successful.')

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'MIPS 5KE')
        exec_command_and_wait_for_pattern(self, 'uname -a',
                                                '3.19.3.mtoman.20150408')
        exec_command_and_wait_for_pattern(self, 'reboot',
                                                'reboot: Restarting system')
        # Wait for VM to shut down gracefully
        self.vm.wait()

    def do_test_mips_malta32el_nanomips(self, kernel_url, kernel_hash):
        kernel_path_xz = self.fetch_asset(kernel_url, asset_hash=kernel_hash)
        kernel_path = self.workdir + "kernel"
        with lzma.open(kernel_path_xz, 'rb') as f_in:
            with open(kernel_path, 'wb') as f_out:
                shutil.copyfileobj(f_in, f_out)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE
                               + 'mem=256m@@0x0 '
                               + 'console=ttyS0')
        self.vm.add_args('-no-reboot',
                         '-cpu', 'I7200',
                         '-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    def test_mips_malta32el_nanomips_4k(self):
        """
        :avocado: tags=arch:mipsel
        :avocado: tags=machine:malta
        :avocado: tags=endian:little
        """
        kernel_url = ('https://mipsdistros.mips.com/LinuxDistro/nanomips/'
                      'kernels/v4.15.18-432-gb2eb9a8b07a1-20180627102142/'
                      'generic_nano32r6el_page4k.xz')
        kernel_hash = '477456aafd2a0f1ddc9482727f20fe9575565dd6'
        self.do_test_mips_malta32el_nanomips(kernel_url, kernel_hash)

    def test_mips_malta32el_nanomips_16k_up(self):
        """
        :avocado: tags=arch:mipsel
        :avocado: tags=machine:malta
        :avocado: tags=endian:little
        """
        kernel_url = ('https://mipsdistros.mips.com/LinuxDistro/nanomips/'
                      'kernels/v4.15.18-432-gb2eb9a8b07a1-20180627102142/'
                      'generic_nano32r6el_page16k_up.xz')
        kernel_hash = 'e882868f944c71c816e832e2303b7874d044a7bc'
        self.do_test_mips_malta32el_nanomips(kernel_url, kernel_hash)

    def test_mips_malta32el_nanomips_64k_dbg(self):
        """
        :avocado: tags=arch:mipsel
        :avocado: tags=machine:malta
        :avocado: tags=endian:little
        """
        kernel_url = ('https://mipsdistros.mips.com/LinuxDistro/nanomips/'
                      'kernels/v4.15.18-432-gb2eb9a8b07a1-20180627102142/'
                      'generic_nano32r6el_page64k_dbg.xz')
        kernel_hash = '18d1c68f2e23429e266ca39ba5349ccd0aeb7180'
        self.do_test_mips_malta32el_nanomips(kernel_url, kernel_hash)

    def test_aarch64_virt(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=machine:virt
        """
        kernel_url = ('https://archives.fedoraproject.org/pub/archive/fedora'
                      '/linux/releases/29/Everything/aarch64/os/images/pxeboot'
                      '/vmlinuz')
        kernel_hash = '8c73e469fc6ea06a58dc83a628fc695b693b8493'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0')
        self.vm.add_args('-cpu', 'cortex-a53',
                         '-kernel', kernel_path,
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
        """
        images_url = ('http://ports.ubuntu.com/ubuntu-ports/dists/'
                      'bionic-updates/main/installer-arm64/'
                      '20101020ubuntu543.15/images/')
        kernel_url = images_url + 'netboot/ubuntu-installer/arm64/linux'
        kernel_hash = '5bfc54cf7ed8157d93f6e5b0241e727b6dc22c50'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        initrd_url = images_url + 'netboot/ubuntu-installer/arm64/initrd.gz'
        initrd_hash = 'd385d3e88d53e2004c5d43cbe668b458a094f772'
        initrd_path = self.fetch_asset(initrd_url, asset_hash=initrd_hash)

        self.vm.set_console()
        self.vm.add_args('-m', '2G',
                         '-kernel', kernel_path,
                         '-initrd', initrd_path)
        self.vm.launch()
        self.wait_for_console_pattern('Checked W+X mappings: passed')

    def test_arm_virt(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:virt
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
        """
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

        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE
        self.vm.add_args('-kernel', uboot_path,
                         '-append', kernel_command_line,
                         '-drive', 'file=' + spi_path + ',if=mtd,format=raw',
                         '-no-reboot')
        self.vm.launch()
        self.wait_for_console_pattern('Enter \'help\' for a list')

        exec_command_and_wait_for_pattern(self, 'ifconfig eth0 10.0.2.15',
                                                 'eth0: link becomes ready')
        exec_command_and_wait_for_pattern(self, 'ping -c 3 10.0.2.2',
            '3 packets transmitted, 3 packets received, 0% packet loss')

    def do_test_arm_raspi2(self, uart_id):
        """
        The kernel can be rebuilt using the kernel source referenced
        and following the instructions on the on:
        https://www.raspberrypi.org/documentation/linux/kernel/building.md
        """
        serial_kernel_cmdline = {
            0: 'earlycon=pl011,0x3f201000 console=ttyAMA0',
        }
        deb_url = ('http://archive.raspberrypi.org/debian/'
                   'pool/main/r/raspberrypi-firmware/'
                   'raspberrypi-kernel_1.20190215-1_armhf.deb')
        deb_hash = 'cd284220b32128c5084037553db3c482426f3972'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path, '/boot/kernel7.img')
        dtb_path = self.extract_from_deb(deb_path, '/boot/bcm2709-rpi-2-b.dtb')

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               serial_kernel_cmdline[uart_id] +
                               ' root=/dev/mmcblk0p2 rootwait ' +
                               'dwc_otg.fiq_fsm_enable=0')
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-append', kernel_command_line,
                         '-device', 'usb-kbd')
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)
        console_pattern = 'Product: QEMU USB Keyboard'
        self.wait_for_console_pattern(console_pattern)

    def test_arm_raspi2_uart0(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:raspi2
        :avocado: tags=device:pl011
        """
        self.do_test_arm_raspi2(0)

    def test_arm_exynos4210_initrd(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:smdkc210
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

    @skipUnless(os.getenv('ARMBIAN_ARTIFACTS_CACHED'),
                'Test artifacts fetched from unreliable apt.armbian.com')
    def test_arm_cubieboard_initrd(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:cubieboard
        """
        deb_url = ('https://apt.armbian.com/pool/main/l/'
                   'linux-4.20.7-sunxi/linux-image-dev-sunxi_5.75_armhf.deb')
        deb_hash = '1334c29c44d984ffa05ed10de8c3361f33d78315'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinuz-4.20.7-sunxi')
        dtb_path = '/usr/lib/linux-image-dev-sunxi/sun4i-a10-cubieboard.dtb'
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
        # cubieboard's reboot is not functioning; omit reboot test.

    @skipUnless(os.getenv('ARMBIAN_ARTIFACTS_CACHED'),
                'Test artifacts fetched from unreliable apt.armbian.com')
    def test_arm_cubieboard_sata(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:cubieboard
        """
        deb_url = ('https://apt.armbian.com/pool/main/l/'
                   'linux-4.20.7-sunxi/linux-image-dev-sunxi_5.75_armhf.deb')
        deb_hash = '1334c29c44d984ffa05ed10de8c3361f33d78315'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinuz-4.20.7-sunxi')
        dtb_path = '/usr/lib/linux-image-dev-sunxi/sun4i-a10-cubieboard.dtb'
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
        # cubieboard's reboot is not functioning; omit reboot test.

    @skipUnless(os.getenv('AVOCADO_TIMEOUT_EXPECTED'), 'Test might timeout')
    def test_arm_quanta_gsj(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:quanta-gsj
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

    @skipUnless(os.getenv('ARMBIAN_ARTIFACTS_CACHED'),
                'Test artifacts fetched from unreliable apt.armbian.com')
    def test_arm_orangepi(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:orangepi-pc
        """
        deb_url = ('https://apt.armbian.com/pool/main/l/'
                   'linux-4.20.7-sunxi/linux-image-dev-sunxi_5.75_armhf.deb')
        deb_hash = '1334c29c44d984ffa05ed10de8c3361f33d78315'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinuz-4.20.7-sunxi')
        dtb_path = '/usr/lib/linux-image-dev-sunxi/sun8i-h3-orangepi-pc.dtb'
        dtb_path = self.extract_from_deb(deb_path, dtb_path)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200n8 '
                               'earlycon=uart,mmio32,0x1c28000')
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    @skipUnless(os.getenv('ARMBIAN_ARTIFACTS_CACHED'),
                'Test artifacts fetched from unreliable apt.armbian.com')
    def test_arm_orangepi_initrd(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:orangepi-pc
        """
        deb_url = ('https://apt.armbian.com/pool/main/l/'
                   'linux-4.20.7-sunxi/linux-image-dev-sunxi_5.75_armhf.deb')
        deb_hash = '1334c29c44d984ffa05ed10de8c3361f33d78315'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinuz-4.20.7-sunxi')
        dtb_path = '/usr/lib/linux-image-dev-sunxi/sun8i-h3-orangepi-pc.dtb'
        dtb_path = self.extract_from_deb(deb_path, dtb_path)
        initrd_url = ('https://github.com/groeck/linux-build-test/raw/'
                      '2eb0a73b5d5a28df3170c546ddaaa9757e1e0848/rootfs/'
                      'arm/rootfs-armv7a.cpio.gz')
        initrd_hash = '604b2e45cdf35045846b8bbfbf2129b1891bdc9c'
        initrd_path_gz = self.fetch_asset(initrd_url, asset_hash=initrd_hash)
        initrd_path = os.path.join(self.workdir, 'rootfs.cpio')
        archive.gzip_uncompress(initrd_path_gz, initrd_path)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200 '
                               'panic=-1 noreboot')
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-initrd', initrd_path,
                         '-append', kernel_command_line,
                         '-no-reboot')
        self.vm.launch()
        self.wait_for_console_pattern('Boot successful.')

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'Allwinner sun8i Family')
        exec_command_and_wait_for_pattern(self, 'cat /proc/iomem',
                                                'system-control@1c00000')
        exec_command_and_wait_for_pattern(self, 'reboot',
                                                'reboot: Restarting system')
        # Wait for VM to shut down gracefully
        self.vm.wait()

    @skipUnless(os.getenv('ARMBIAN_ARTIFACTS_CACHED'),
                'Test artifacts fetched from unreliable apt.armbian.com')
    def test_arm_orangepi_sd(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:orangepi-pc
        :avocado: tags=device:sd
        """
        deb_url = ('https://apt.armbian.com/pool/main/l/'
                   'linux-4.20.7-sunxi/linux-image-dev-sunxi_5.75_armhf.deb')
        deb_hash = '1334c29c44d984ffa05ed10de8c3361f33d78315'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinuz-4.20.7-sunxi')
        dtb_path = '/usr/lib/linux-image-dev-sunxi/sun8i-h3-orangepi-pc.dtb'
        dtb_path = self.extract_from_deb(deb_path, dtb_path)
        rootfs_url = ('http://storage.kernelci.org/images/rootfs/buildroot/'
                      'kci-2019.02/armel/base/rootfs.ext2.xz')
        rootfs_hash = '692510cb625efda31640d1de0a8d60e26040f061'
        rootfs_path_xz = self.fetch_asset(rootfs_url, asset_hash=rootfs_hash)
        rootfs_path = os.path.join(self.workdir, 'rootfs.cpio')
        archive.lzma_uncompress(rootfs_path_xz, rootfs_path)
        image_pow2ceil_expand(rootfs_path)

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200 '
                               'root=/dev/mmcblk0 rootwait rw '
                               'panic=-1 noreboot')
        self.vm.add_args('-kernel', kernel_path,
                         '-dtb', dtb_path,
                         '-drive', 'file=' + rootfs_path + ',if=sd,format=raw',
                         '-append', kernel_command_line,
                         '-no-reboot')
        self.vm.launch()
        shell_ready = "/bin/sh: can't access tty; job control turned off"
        self.wait_for_console_pattern(shell_ready)

        exec_command_and_wait_for_pattern(self, 'cat /proc/cpuinfo',
                                                'Allwinner sun8i Family')
        exec_command_and_wait_for_pattern(self, 'cat /proc/partitions',
                                                'mmcblk0')
        exec_command_and_wait_for_pattern(self, 'ifconfig eth0 up',
                                                 'eth0: Link is Up')
        exec_command_and_wait_for_pattern(self, 'udhcpc eth0',
            'udhcpc: lease of 10.0.2.15 obtained')
        exec_command_and_wait_for_pattern(self, 'ping -c 3 10.0.2.2',
            '3 packets transmitted, 3 packets received, 0% packet loss')
        exec_command_and_wait_for_pattern(self, 'reboot',
                                                'reboot: Restarting system')
        # Wait for VM to shut down gracefully
        self.vm.wait()

    def do_test_arm_orangepi_uboot_armbian(self, image_path):
        self.vm.set_console()
        self.vm.add_args('-drive', 'file=' + image_path + ',if=sd,format=raw',
                         '-nic', 'user',
                         '-no-reboot')
        self.vm.launch()

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200 '
                               'loglevel=7 '
                               'nosmp '
                               'systemd.default_timeout_start_sec=9000 '
                               'systemd.mask=armbian-zram-config.service '
                               'systemd.mask=armbian-ramlog.service')

        self.wait_for_console_pattern('U-Boot SPL')
        self.wait_for_console_pattern('Autoboot in ')
        exec_command_and_wait_for_pattern(self, ' ', '=>')
        exec_command_and_wait_for_pattern(self, "setenv extraargs '" +
                                                kernel_command_line + "'", '=>')
        exec_command_and_wait_for_pattern(self, 'boot', 'Starting kernel ...');

        self.wait_for_console_pattern('systemd[1]: Set hostname ' +
                                      'to <orangepipc>')
        self.wait_for_console_pattern('Starting Load Kernel Modules...')

    @skipUnless(os.getenv('ARMBIAN_ARTIFACTS_CACHED'),
                'Test artifacts fetched from unreliable apt.armbian.com')
    @skipUnless(os.getenv('AVOCADO_ALLOW_LARGE_STORAGE'), 'storage limited')
    @skipUnless(P7ZIP_AVAILABLE, '7z not installed')
    def test_arm_orangepi_bionic_19_11(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:orangepi-pc
        :avocado: tags=device:sd
        """

        # This test download a 196MB compressed image and expand it to 1GB
        image_url = ('https://dl.armbian.com/orangepipc/archive/'
                     'Armbian_19.11.3_Orangepipc_bionic_current_5.3.9.7z')
        image_hash = '196a8ffb72b0123d92cea4a070894813d305c71e'
        image_path_7z = self.fetch_asset(image_url, asset_hash=image_hash)
        image_name = 'Armbian_19.11.3_Orangepipc_bionic_current_5.3.9.img'
        image_path = os.path.join(self.workdir, image_name)
        process.run("7z e -o%s %s" % (self.workdir, image_path_7z))
        image_pow2ceil_expand(image_path)

        self.do_test_arm_orangepi_uboot_armbian(image_path)

    @skipUnless(os.getenv('ARMBIAN_ARTIFACTS_CACHED'),
                'Test artifacts fetched from unreliable apt.armbian.com')
    @skipUnless(os.getenv('AVOCADO_ALLOW_LARGE_STORAGE'), 'storage limited')
    def test_arm_orangepi_bionic_20_08(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:orangepi-pc
        :avocado: tags=device:sd
        """

        # This test download a 275 MiB compressed image and expand it
        # to 1036 MiB, but the underlying filesystem is 1552 MiB...
        # As we expand it to 2 GiB we are safe.

        image_url = ('https://dl.armbian.com/orangepipc/archive/'
                     'Armbian_20.08.1_Orangepipc_bionic_current_5.8.5.img.xz')
        image_hash = ('b4d6775f5673486329e45a0586bf06b6'
                      'dbe792199fd182ac6b9c7bb6c7d3e6dd')
        image_path_xz = self.fetch_asset(image_url, asset_hash=image_hash,
                                         algorithm='sha256')
        image_path = archive.extract(image_path_xz, self.workdir)
        image_pow2ceil_expand(image_path)

        self.do_test_arm_orangepi_uboot_armbian(image_path)

    @skipUnless(os.getenv('AVOCADO_ALLOW_LARGE_STORAGE'), 'storage limited')
    def test_arm_orangepi_uboot_netbsd9(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:orangepi-pc
        :avocado: tags=device:sd
        """
        # This test download a 304MB compressed image and expand it to 2GB
        deb_url = ('http://snapshot.debian.org/archive/debian/'
                   '20200108T145233Z/pool/main/u/u-boot/'
                   'u-boot-sunxi_2020.01%2Bdfsg-1_armhf.deb')
        deb_hash = 'f67f404a80753ca3d1258f13e38f2b060e13db99'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        # We use the common OrangePi PC 'plus' build of U-Boot for our secondary
        # program loader (SPL). We will then set the path to the more specific
        # OrangePi "PC" device tree blob with 'setenv fdtfile' in U-Boot prompt,
        # before to boot NetBSD.
        uboot_path = '/usr/lib/u-boot/orangepi_plus/u-boot-sunxi-with-spl.bin'
        uboot_path = self.extract_from_deb(deb_path, uboot_path)
        image_url = ('https://cdn.netbsd.org/pub/NetBSD/NetBSD-9.0/'
                     'evbarm-earmv7hf/binary/gzimg/armv7.img.gz')
        image_hash = '2babb29d36d8360adcb39c09e31060945259917a'
        image_path_gz = self.fetch_asset(image_url, asset_hash=image_hash)
        image_path = os.path.join(self.workdir, 'armv7.img')
        archive.gzip_uncompress(image_path_gz, image_path)
        image_pow2ceil_expand(image_path)
        image_drive_args = 'if=sd,format=raw,snapshot=on,file=' + image_path

        # dd if=u-boot-sunxi-with-spl.bin of=armv7.img bs=1K seek=8 conv=notrunc
        with open(uboot_path, 'rb') as f_in:
            with open(image_path, 'r+b') as f_out:
                f_out.seek(8 * 1024)
                shutil.copyfileobj(f_in, f_out)

        self.vm.set_console()
        self.vm.add_args('-nic', 'user',
                         '-drive', image_drive_args,
                         '-global', 'allwinner-rtc.base-year=2000',
                         '-no-reboot')
        self.vm.launch()
        wait_for_console_pattern(self, 'U-Boot 2020.01+dfsg-1')
        interrupt_interactive_console_until_pattern(self,
                                       'Hit any key to stop autoboot:',
                                       'switch to partitions #0, OK')

        exec_command_and_wait_for_pattern(self, '', '=>')
        cmd = 'setenv bootargs root=ld0a'
        exec_command_and_wait_for_pattern(self, cmd, '=>')
        cmd = 'setenv kernel netbsd-GENERIC.ub'
        exec_command_and_wait_for_pattern(self, cmd, '=>')
        cmd = 'setenv fdtfile dtb/sun8i-h3-orangepi-pc.dtb'
        exec_command_and_wait_for_pattern(self, cmd, '=>')
        cmd = ("setenv bootcmd 'fatload mmc 0:1 ${kernel_addr_r} ${kernel}; "
               "fatload mmc 0:1 ${fdt_addr_r} ${fdtfile}; "
               "fdt addr ${fdt_addr_r}; "
               "bootm ${kernel_addr_r} - ${fdt_addr_r}'")
        exec_command_and_wait_for_pattern(self, cmd, '=>')

        exec_command_and_wait_for_pattern(self, 'boot',
                                          'Booting kernel from Legacy Image')
        wait_for_console_pattern(self, 'Starting kernel ...')
        wait_for_console_pattern(self, 'NetBSD 9.0 (GENERIC)')
        # Wait for user-space
        wait_for_console_pattern(self, 'Starting root file system check')

    def test_aarch64_raspi3_atf(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=machine:raspi3
        :avocado: tags=cpu:cortex-a53
        :avocado: tags=device:pl011
        :avocado: tags=atf
        """
        zip_url = ('https://github.com/pbatard/RPi3/releases/download/'
                   'v1.15/RPi3_UEFI_Firmware_v1.15.zip')
        zip_hash = '74b3bd0de92683cadb14e008a7575e1d0c3cafb9'
        zip_path = self.fetch_asset(zip_url, asset_hash=zip_hash)

        archive.extract(zip_path, self.workdir)
        efi_fd = os.path.join(self.workdir, 'RPI_EFI.fd')

        self.vm.set_console(console_index=1)
        self.vm.add_args('-nodefaults',
                         '-device', 'loader,file=%s,force-raw=true' % efi_fd)
        self.vm.launch()
        self.wait_for_console_pattern('version UEFI Firmware v1.15')

    def test_s390x_s390_ccw_virtio(self):
        """
        :avocado: tags=arch:s390x
        :avocado: tags=machine:s390-ccw-virtio
        """
        kernel_url = ('https://archives.fedoraproject.org/pub/archive'
                      '/fedora-secondary/releases/29/Everything/s390x/os/images'
                      '/kernel.img')
        kernel_hash = 'e8e8439103ef8053418ef062644ffd46a7919313'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=sclp0'
        self.vm.add_args('-nodefaults',
                         '-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    def test_alpha_clipper(self):
        """
        :avocado: tags=arch:alpha
        :avocado: tags=machine:clipper
        """
        kernel_url = ('http://archive.debian.org/debian/dists/lenny/main/'
                      'installer-alpha/20090123lenny10/images/cdrom/vmlinuz')
        kernel_hash = '3a943149335529e2ed3e74d0d787b85fb5671ba3'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        uncompressed_kernel = archive.uncompress(kernel_path, self.workdir)

        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        self.vm.add_args('-nodefaults',
                         '-kernel', uncompressed_kernel,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)

    def test_m68k_q800(self):
        """
        :avocado: tags=arch:m68k
        :avocado: tags=machine:q800
        """
        deb_url = ('https://snapshot.debian.org/archive/debian-ports'
                   '/20191021T083923Z/pool-m68k/main'
                   '/l/linux/kernel-image-5.3.0-1-m68k-di_5.3.7-1_m68k.udeb')
        deb_hash = '044954bb9be4160a3ce81f8bc1b5e856b75cccd1'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinux-5.3.0-1-m68k')

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0 vga=off')
        self.vm.add_args('-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.launch()
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.wait_for_console_pattern(console_pattern)
        console_pattern = 'No filesystem could mount root'
        self.wait_for_console_pattern(console_pattern)

    def do_test_advcal_2018(self, day, tar_hash, kernel_name, console=0):
        tar_url = ('https://www.qemu-advent-calendar.org'
                   '/2018/download/day' + day + '.tar.xz')
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        archive.extract(file_path, self.workdir)
        self.vm.set_console(console_index=console)
        self.vm.add_args('-kernel',
                         self.workdir + '/day' + day + '/' + kernel_name)
        self.vm.launch()
        self.wait_for_console_pattern('QEMU advent calendar')

    def test_arm_vexpressa9(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:vexpress-a9
        """
        tar_hash = '32b7677ce8b6f1471fb0059865f451169934245b'
        self.vm.add_args('-dtb', self.workdir + '/day16/vexpress-v2p-ca9.dtb')
        self.do_test_advcal_2018('16', tar_hash, 'winter.zImage')

    def test_m68k_mcf5208evb(self):
        """
        :avocado: tags=arch:m68k
        :avocado: tags=machine:mcf5208evb
        """
        tar_hash = 'ac688fd00561a2b6ce1359f9ff6aa2b98c9a570c'
        self.do_test_advcal_2018('07', tar_hash, 'sanity-clause.elf')

    def test_or1k_sim(self):
        """
        :avocado: tags=arch:or1k
        :avocado: tags=machine:or1k-sim
        """
        tar_hash = '20334cdaf386108c530ff0badaecc955693027dd'
        self.do_test_advcal_2018('20', tar_hash, 'vmlinux')

    def test_nios2_10m50(self):
        """
        :avocado: tags=arch:nios2
        :avocado: tags=machine:10m50-ghrd
        """
        tar_hash = 'e4251141726c412ac0407c5a6bceefbbff018918'
        self.do_test_advcal_2018('14', tar_hash, 'vmlinux.elf')

    def test_ppc64_e500(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:ppce500
        """
        tar_hash = '6951d86d644b302898da2fd701739c9406527fe1'
        self.vm.add_args('-cpu', 'e5500')
        self.do_test_advcal_2018('19', tar_hash, 'uImage')

    def test_ppc_g3beige(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:g3beige
        """
        tar_hash = 'e0b872a5eb8fdc5bed19bd43ffe863900ebcedfc'
        self.vm.add_args('-M', 'graphics=off')
        self.do_test_advcal_2018('15', tar_hash, 'invaders.elf')

    def test_ppc_mac99(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:mac99
        """
        tar_hash = 'e0b872a5eb8fdc5bed19bd43ffe863900ebcedfc'
        self.vm.add_args('-M', 'graphics=off')
        self.do_test_advcal_2018('15', tar_hash, 'invaders.elf')

    def test_sh4_r2d(self):
        """
        :avocado: tags=arch:sh4
        :avocado: tags=machine:r2d
        """
        tar_hash = 'fe06a4fd8ccbf2e27928d64472939d47829d4c7e'
        self.vm.add_args('-append', 'console=ttySC1')
        self.do_test_advcal_2018('09', tar_hash, 'zImage', console=1)

    def test_sparc_ss20(self):
        """
        :avocado: tags=arch:sparc
        :avocado: tags=machine:SS-20
        """
        tar_hash = 'b18550d5d61c7615d989a06edace051017726a9f'
        self.do_test_advcal_2018('11', tar_hash, 'zImage.elf')

    def test_xtensa_lx60(self):
        """
        :avocado: tags=arch:xtensa
        :avocado: tags=machine:lx60
        """
        tar_hash = '49e88d9933742f0164b60839886c9739cb7a0d34'
        self.vm.add_args('-cpu', 'dc233c')
        self.do_test_advcal_2018('02', tar_hash, 'santas-sleigh-ride.elf')
