# Record/replay test that boots a Linux kernel
#
# Copyright (c) 2020 ISP RAS
#
# Author:
#  Pavel Dovgalyuk <Pavel.Dovgaluk@ispras.ru>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os
import lzma
import shutil
import logging
import time

from avocado import skip
from avocado import skipIf
from avocado import skipUnless
from avocado_qemu import wait_for_console_pattern
from avocado.utils import archive
from avocado.utils import process
from boot_linux_console import LinuxKernelTest

class ReplayKernelBase(LinuxKernelTest):
    """
    Boots a Linux kernel in record mode and checks that the console
    is operational and the kernel command line is properly passed
    from QEMU to the kernel.
    Then replays the same scenario and verifies, that QEMU correctly
    terminates.
    """

    timeout = 120
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=1 panic=-1 '

    def run_vm(self, kernel_path, kernel_command_line, console_pattern,
               record, shift, args, replay_path):
        # icount requires TCG to be available
        self.require_accelerator('tcg')

        logger = logging.getLogger('replay')
        start_time = time.time()
        vm = self.get_vm()
        vm.set_console()
        if record:
            logger.info('recording the execution...')
            mode = 'record'
        else:
            logger.info('replaying the execution...')
            mode = 'replay'
        vm.add_args('-icount', 'shift=%s,rr=%s,rrfile=%s' %
                    (shift, mode, replay_path),
                    '-kernel', kernel_path,
                    '-append', kernel_command_line,
                    '-net', 'none',
                    '-no-reboot')
        if args:
            vm.add_args(*args)
        vm.launch()
        self.wait_for_console_pattern(console_pattern, vm)
        if record:
            vm.shutdown()
            logger.info('finished the recording with log size %s bytes'
                        % os.path.getsize(replay_path))
        else:
            vm.wait()
            logger.info('successfully finished the replay')
        elapsed = time.time() - start_time
        logger.info('elapsed time %.2f sec' % elapsed)
        return elapsed

    def run_rr(self, kernel_path, kernel_command_line, console_pattern,
               shift=7, args=None):
        replay_path = os.path.join(self.workdir, 'replay.bin')
        t1 = self.run_vm(kernel_path, kernel_command_line, console_pattern,
                         True, shift, args, replay_path)
        t2 = self.run_vm(kernel_path, kernel_command_line, console_pattern,
                         False, shift, args, replay_path)
        logger = logging.getLogger('replay')
        logger.info('replay overhead {:.2%}'.format(t2 / t1 - 1))

class ReplayKernelNormal(ReplayKernelBase):
    @skipIf(os.getenv('GITLAB_CI'), 'Running on GitLab')
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

        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        console_pattern = 'VFS: Cannot open root device'

        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=5)

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
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        console_pattern = 'Kernel command line: %s' % kernel_command_line

        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=5)

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
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=5)

    def test_aarch64_virt(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=machine:virt
        :avocado: tags=cpu:cortex-a53
        """
        kernel_url = ('https://archives.fedoraproject.org/pub/archive/fedora'
                      '/linux/releases/29/Everything/aarch64/os/images/pxeboot'
                      '/vmlinuz')
        kernel_hash = '8c73e469fc6ea06a58dc83a628fc695b693b8493'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0')
        console_pattern = 'VFS: Cannot open root device'

        self.run_rr(kernel_path, kernel_command_line, console_pattern)

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

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0')
        console_pattern = 'VFS: Cannot open root device'

        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=1)

    @skipIf(os.getenv('GITLAB_CI'), 'Running on GitLab')
    def test_arm_cubieboard_initrd(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:cubieboard
        """
        deb_url = ('https://apt.armbian.com/pool/main/l/'
                   'linux-5.10.16-sunxi/linux-image-current-sunxi_21.02.2_armhf.deb')
        deb_hash = '9fa84beda245cabf0b4fa84cf6eaa7738ead1da0'
        deb_path = self.fetch_asset(deb_url, asset_hash=deb_hash)
        kernel_path = self.extract_from_deb(deb_path,
                                            '/boot/vmlinuz-5.10.16-sunxi')
        dtb_path = '/usr/lib/linux-image-current-sunxi/sun4i-a10-cubieboard.dtb'
        dtb_path = self.extract_from_deb(deb_path, dtb_path)
        initrd_url = ('https://github.com/groeck/linux-build-test/raw/'
                      '2eb0a73b5d5a28df3170c546ddaaa9757e1e0848/rootfs/'
                      'arm/rootfs-armv5.cpio.gz')
        initrd_hash = '2b50f1873e113523967806f4da2afe385462ff9b'
        initrd_path_gz = self.fetch_asset(initrd_url, asset_hash=initrd_hash)
        initrd_path = os.path.join(self.workdir, 'rootfs.cpio')
        archive.gzip_uncompress(initrd_path_gz, initrd_path)

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0,115200 '
                               'usbcore.nousb '
                               'panic=-1 noreboot')
        console_pattern = 'Boot successful.'
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=1,
                    args=('-dtb', dtb_path,
                          '-initrd', initrd_path,
                          '-no-reboot'))

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

        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=sclp0'
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=9)

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

        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=ttyS0'
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.run_rr(uncompressed_kernel, kernel_command_line, console_pattern, shift=9,
            args=('-nodefaults', ))

    def test_ppc64_pseries(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=accel:tcg
        """
        kernel_url = ('https://archives.fedoraproject.org/pub/archive'
                      '/fedora-secondary/releases/29/Everything/ppc64le/os'
                      '/ppc/ppc64/vmlinuz')
        kernel_hash = '3fe04abfc852b66653b8c3c897a59a689270bc77'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE + 'console=hvc0'
        # icount is not good enough for PPC64 for complete boot yet
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.run_rr(kernel_path, kernel_command_line, console_pattern)

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

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0 vga=off')
        console_pattern = 'No filesystem could mount root'
        self.run_rr(kernel_path, kernel_command_line, console_pattern)

    def do_test_advcal_2018(self, file_path, kernel_name, args=None):
        archive.extract(file_path, self.workdir)

        for entry in os.scandir(self.workdir):
            if entry.name.startswith('day') and entry.is_dir():
                kernel_path = os.path.join(entry.path, kernel_name)
                break

        kernel_command_line = ''
        console_pattern = 'QEMU advent calendar'
        self.run_rr(kernel_path, kernel_command_line, console_pattern,
                    args=args)

    def test_arm_vexpressa9(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=machine:vexpress-a9
        """
        tar_hash = '32b7677ce8b6f1471fb0059865f451169934245b'
        tar_url = ('https://qemu-advcal.gitlab.io'
                   '/qac-best-of-multiarch/download/day16.tar.xz')
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        dtb_path = self.workdir + '/day16/vexpress-v2p-ca9.dtb'
        self.do_test_advcal_2018(file_path, 'winter.zImage',
                                 args=('-dtb', dtb_path))

    def test_m68k_mcf5208evb(self):
        """
        :avocado: tags=arch:m68k
        :avocado: tags=machine:mcf5208evb
        """
        tar_hash = 'ac688fd00561a2b6ce1359f9ff6aa2b98c9a570c'
        tar_url = ('https://qemu-advcal.gitlab.io'
                   '/qac-best-of-multiarch/download/day07.tar.xz')
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        self.do_test_advcal_2018(file_path, 'sanity-clause.elf')

    @skip("Test currently broken") # Console stuck as of 5.2-rc1
    def test_microblaze_s3adsp1800(self):
        """
        :avocado: tags=arch:microblaze
        :avocado: tags=machine:petalogix-s3adsp1800
        """
        tar_hash = '08bf3e3bfb6b6c7ce1e54ab65d54e189f2caf13f'
        tar_url = ('https://qemu-advcal.gitlab.io'
                   '/qac-best-of-multiarch/download/day17.tar.xz')
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        self.do_test_advcal_2018(file_path, 'ballerina.bin')

    def test_ppc64_e500(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:ppce500
        :avocado: tags=cpu:e5500
        """
        tar_hash = '6951d86d644b302898da2fd701739c9406527fe1'
        tar_url = ('https://qemu-advcal.gitlab.io'
                   '/qac-best-of-multiarch/download/day19.tar.xz')
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        self.do_test_advcal_2018(file_path, 'uImage')

    def test_or1k_sim(self):
        """
        :avocado: tags=arch:or1k
        :avocado: tags=machine:or1k-sim
        """
        tar_hash = '20334cdaf386108c530ff0badaecc955693027dd'
        tar_url = ('https://qemu-advcal.gitlab.io'
                   '/qac-best-of-multiarch/download/day20.tar.xz')
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        self.do_test_advcal_2018(file_path, 'vmlinux')

    @skip("nios2 emulation is buggy under record/replay")
    def test_nios2_10m50(self):
        """
        :avocado: tags=arch:nios2
        :avocado: tags=machine:10m50-ghrd
        """
        tar_hash = 'e4251141726c412ac0407c5a6bceefbbff018918'
        tar_url = ('https://qemu-advcal.gitlab.io'
                   '/qac-best-of-multiarch/download/day14.tar.xz')
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        self.do_test_advcal_2018(file_path, 'vmlinux.elf')

    def test_ppc_g3beige(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:g3beige
        """
        tar_hash = 'e0b872a5eb8fdc5bed19bd43ffe863900ebcedfc'
        tar_url = ('https://qemu-advcal.gitlab.io'
                   '/qac-best-of-multiarch/download/day15.tar.xz')
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        self.do_test_advcal_2018(file_path, 'invaders.elf',
                                 args=('-M', 'graphics=off'))

    def test_ppc_mac99(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:mac99
        """
        tar_hash = 'e0b872a5eb8fdc5bed19bd43ffe863900ebcedfc'
        tar_url = ('https://qemu-advcal.gitlab.io'
                   '/qac-best-of-multiarch/download/day15.tar.xz')
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        self.do_test_advcal_2018(file_path, 'invaders.elf',
                                 args=('-M', 'graphics=off'))

    def test_sparc_ss20(self):
        """
        :avocado: tags=arch:sparc
        :avocado: tags=machine:SS-20
        """
        tar_hash = 'b18550d5d61c7615d989a06edace051017726a9f'
        tar_url = ('https://qemu-advcal.gitlab.io'
                   '/qac-best-of-multiarch/download/day11.tar.xz')
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        self.do_test_advcal_2018(file_path, 'zImage.elf')

    def test_xtensa_lx60(self):
        """
        :avocado: tags=arch:xtensa
        :avocado: tags=machine:lx60
        :avocado: tags=cpu:dc233c
        """
        tar_hash = '49e88d9933742f0164b60839886c9739cb7a0d34'
        tar_url = ('https://qemu-advcal.gitlab.io'
                   '/qac-best-of-multiarch/download/day02.tar.xz')
        file_path = self.fetch_asset(tar_url, asset_hash=tar_hash)
        self.do_test_advcal_2018(file_path, 'santas-sleigh-ride.elf')

@skipUnless(os.getenv('AVOCADO_TIMEOUT_EXPECTED'), 'Test might timeout')
class ReplayKernelSlow(ReplayKernelBase):
    # Override the timeout, because this kernel includes an inner
    # loop which is executed with TB recompilings during replay,
    # making it very slow.
    timeout = 180

    def test_mips_malta_cpio(self):
        """
        :avocado: tags=arch:mips
        :avocado: tags=machine:malta
        :avocado: tags=endian:big
        :avocado: tags=slowness:high
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

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0 console=tty '
                               'rdinit=/sbin/init noreboot')
        console_pattern = 'Boot successful.'
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=5,
                    args=('-initrd', initrd_path))

    @skipUnless(os.getenv('AVOCADO_ALLOW_UNTRUSTED_CODE'), 'untrusted code')
    def test_mips64el_malta_5KEc_cpio(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=machine:malta
        :avocado: tags=endian:little
        :avocado: tags=slowness:high
        :avocado: tags=cpu:5KEc
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

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyS0 console=tty '
                               'rdinit=/sbin/init noreboot')
        console_pattern = 'Boot successful.'
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=5,
                    args=('-initrd', initrd_path))

    def do_test_mips_malta32el_nanomips(self, kernel_path_xz):
        kernel_path = self.workdir + "kernel"
        with lzma.open(kernel_path_xz, 'rb') as f_in:
            with open(kernel_path, 'wb') as f_out:
                shutil.copyfileobj(f_in, f_out)

        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'mem=256m@@0x0 '
                               'console=ttyS0')
        console_pattern = 'Kernel command line: %s' % kernel_command_line
        self.run_rr(kernel_path, kernel_command_line, console_pattern, shift=5)

    def test_mips_malta32el_nanomips_4k(self):
        """
        :avocado: tags=arch:mipsel
        :avocado: tags=machine:malta
        :avocado: tags=endian:little
        :avocado: tags=cpu:I7200
        """
        kernel_url = ('http://mipsdistros.mips.com/LinuxDistro/nanomips/'
                      'kernels/v4.15.18-432-gb2eb9a8b07a1-20180627102142/'
                      'generic_nano32r6el_page4k.xz')
        kernel_hash = '477456aafd2a0f1ddc9482727f20fe9575565dd6'
        kernel_path_xz = self.fetch_asset(kernel_url, asset_hash=kernel_hash)
        self.do_test_mips_malta32el_nanomips(kernel_path_xz)

    def test_mips_malta32el_nanomips_16k_up(self):
        """
        :avocado: tags=arch:mipsel
        :avocado: tags=machine:malta
        :avocado: tags=endian:little
        :avocado: tags=cpu:I7200
        """
        kernel_url = ('http://mipsdistros.mips.com/LinuxDistro/nanomips/'
                      'kernels/v4.15.18-432-gb2eb9a8b07a1-20180627102142/'
                      'generic_nano32r6el_page16k_up.xz')
        kernel_hash = 'e882868f944c71c816e832e2303b7874d044a7bc'
        kernel_path_xz = self.fetch_asset(kernel_url, asset_hash=kernel_hash)
        self.do_test_mips_malta32el_nanomips(kernel_path_xz)

    def test_mips_malta32el_nanomips_64k_dbg(self):
        """
        :avocado: tags=arch:mipsel
        :avocado: tags=machine:malta
        :avocado: tags=endian:little
        :avocado: tags=cpu:I7200
        """
        kernel_url = ('http://mipsdistros.mips.com/LinuxDistro/nanomips/'
                      'kernels/v4.15.18-432-gb2eb9a8b07a1-20180627102142/'
                      'generic_nano32r6el_page64k_dbg.xz')
        kernel_hash = '18d1c68f2e23429e266ca39ba5349ccd0aeb7180'
        kernel_path_xz = self.fetch_asset(kernel_url, asset_hash=kernel_hash)
        self.do_test_mips_malta32el_nanomips(kernel_path_xz)
