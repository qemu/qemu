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

import os
import time
import tempfile

from avocado import skip, skipUnless
from avocado_qemu import QemuSystemTest
from avocado_qemu import exec_command, exec_command_and_wait_for_pattern
from avocado_qemu import wait_for_console_pattern
from avocado.utils import process
from avocado.utils.path import find_command

class TuxRunBaselineTest(QemuSystemTest):
    """
    :avocado: tags=accel:tcg
    """

    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0'
    # Tests are ~10-40s, allow for --debug/--enable-gcov overhead
    timeout = 100

    def get_tag(self, tagname, default=None):
        """
        Get the metadata tag or return the default.
        """
        utag = self._get_unique_tag_val(tagname)
        print(f"{tagname}/{default} -> {utag}")
        if utag:
            return utag

        return default

    def setUp(self):
        super().setUp()

        # We need zstd for all the tuxrun tests
        # See https://github.com/avocado-framework/avocado/issues/5609
        zstd = find_command('zstd', False)
        if zstd is False:
            self.cancel('Could not find "zstd", which is required to '
                        'decompress rootfs')
        self.zstd = zstd

        # Process the TuxRun specific tags, most machines work with
        # reasonable defaults but we sometimes need to tweak the
        # config. To avoid open coding everything we store all these
        # details in the metadata for each test.

        # The tuxboot tag matches the root directory
        self.tuxboot = self.get_tag('tuxboot')

        # Most Linux's use ttyS0 for their serial port
        self.console = self.get_tag('console', "ttyS0")

        # Does the machine shutdown QEMU nicely on "halt"
        self.shutdown = self.get_tag('shutdown')

        # The name of the kernel Image file
        self.image = self.get_tag('image', "Image")

        self.root = self.get_tag('root', "vda")

        # Occasionally we need extra devices to hook things up
        self.extradev = self.get_tag('extradev')

        self.qemu_img = super().get_qemu_img()

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    def fetch_tuxrun_assets(self, csums=None, dt=None):
        """
        Fetch the TuxBoot assets. They are stored in a standard way so we
        use the per-test tags to fetch details.
        """
        base_url = f"https://storage.tuxboot.com/20230331/{self.tuxboot}/"

        # empty hash if we weren't passed one
        csums = {} if csums is None else csums
        ksum = csums.get(self.image, None)
        isum = csums.get("rootfs.ext4.zst", None)

        kernel_image =  self.fetch_asset(base_url + self.image,
                                         asset_hash = ksum,
                                         algorithm = "sha256")
        disk_image_zst = self.fetch_asset(base_url + "rootfs.ext4.zst",
                                         asset_hash = isum,
                                         algorithm = "sha256")

        cmd = f"{self.zstd} -d {disk_image_zst} -o {self.workdir}/rootfs.ext4"
        process.run(cmd)

        if dt:
            dsum = csums.get(dt, None)
            dtb = self.fetch_asset(base_url + dt,
                                   asset_hash = dsum,
                                   algorithm = "sha256")
        else:
            dtb = None

        return (kernel_image, self.workdir + "/rootfs.ext4", dtb)

    def prepare_run(self, kernel, disk, drive, dtb=None, console_index=0):
        """
        Setup to run and add the common parameters to the system
        """
        self.vm.set_console(console_index=console_index)

        # all block devices are raw ext4's
        blockdev = "driver=raw,file.driver=file," \
            + f"file.filename={disk},node-name=hd0"

        kcmd_line = self.KERNEL_COMMON_COMMAND_LINE
        kcmd_line += f" root=/dev/{self.root}"
        kcmd_line += f" console={self.console}"

        self.vm.add_args('-kernel', kernel,
                         '-append', kcmd_line,
                         '-blockdev', blockdev)

        # Sometimes we need extra devices attached
        if self.extradev:
            self.vm.add_args('-device', self.extradev)

        self.vm.add_args('-device',
                         f"{drive},drive=hd0")

        # Some machines need an explicit DTB
        if dtb:
            self.vm.add_args('-dtb', dtb)

    def run_tuxtest_tests(self, haltmsg):
        """
        Wait for the system to boot up, wait for the login prompt and
        then do a few things on the console. Trigger a shutdown and
        wait to exit cleanly.
        """
        self.wait_for_console_pattern("Welcome to TuxTest")
        time.sleep(0.2)
        exec_command(self, 'root')
        time.sleep(0.2)
        exec_command(self, 'cat /proc/interrupts')
        time.sleep(0.1)
        exec_command(self, 'cat /proc/self/maps')
        time.sleep(0.1)
        exec_command(self, 'uname -a')
        time.sleep(0.1)
        exec_command_and_wait_for_pattern(self, 'halt', haltmsg)

        # Wait for VM to shut down gracefully if it can
        if self.shutdown == "nowait":
            self.vm.shutdown()
        else:
            self.vm.wait()

    def common_tuxrun(self,
                      csums=None,
                      dt=None,
                      drive="virtio-blk-device",
                      haltmsg="reboot: System halted",
                      console_index=0):
        """
        Common path for LKFT tests. Unless we need to do something
        special with the command line we can process most things using
        the tag metadata.
        """
        (kernel, disk, dtb) = self.fetch_tuxrun_assets(csums, dt)

        self.prepare_run(kernel, disk, drive, dtb, console_index)
        self.vm.launch()
        self.run_tuxtest_tests(haltmsg)

    def ppc64_common_tuxrun(self, sums, prefix):
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
        self.vm.add_args('-m', '2G,slots=32,maxmem=4G',
                         '-object', 'memory-backend-ram,id=ram1,size=1G',
                         '-device', 'pc-dimm,id=dimm1,memdev=ram1')

        # Create a temporary qcow2 and launch the test-case
        with tempfile.NamedTemporaryFile(prefix=prefix,
                                         suffix='.qcow2') as qcow2:
            process.run(self.qemu_img + ' create -f qcow2 ' +
                        qcow2.name + ' 1G')

            self.vm.add_args('-drive', 'file=' + qcow2.name +
                         ',format=qcow2,if=none,id='
                         'drive-virtio-disk1',
                         '-device', 'virtio-blk-pci,scsi=off,bus=pci.0,'
                         'addr=0xb,drive=drive-virtio-disk1,id=virtio-disk1'
                         ',bootindex=2')
            self.common_tuxrun(csums=sums, drive="scsi-hd")

    #
    # The tests themselves. The configuration is derived from how
    # tuxrun invokes qemu (with minor tweaks like using -blockdev
    # consistently). The tuxrun equivalent is something like:
    #
    # tuxrun --device qemu-{ARCH} \
    #        --kernel https://storage.tuxboot.com/{TUXBOOT}/{IMAGE}
    #

    def test_arm64(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=cpu:cortex-a57
        :avocado: tags=machine:virt
        :avocado: tags=tuxboot:arm64
        :avocado: tags=console:ttyAMA0
        :avocado: tags=shutdown:nowait
        """
        sums = {"Image" :
                "ce95a7101a5fecebe0fe630deee6bd97b32ba41bc8754090e9ad8961ea8674c7",
                "rootfs.ext4.zst" :
                "bbd5ed4b9c7d3f4ca19ba71a323a843c6b585e880115df3b7765769dbd9dd061"}
        self.common_tuxrun(csums=sums)

    def test_arm64be(self):
        """
        :avocado: tags=arch:aarch64
        :avocado: tags=cpu:cortex-a57
        :avocado: tags=endian:big
        :avocado: tags=machine:virt
        :avocado: tags=tuxboot:arm64be
        :avocado: tags=console:ttyAMA0
        :avocado: tags=shutdown:nowait
        """
        sums = { "Image" :
                 "e0df4425eb2cd9ea9a283e808037f805641c65d8fcecc8f6407d8f4f339561b4",
                 "rootfs.ext4.zst" :
                 "e6ffd8813c8a335bc15728f2835f90539c84be7f8f5f691a8b01451b47fb4bd7"}
        self.common_tuxrun(csums=sums)

    def test_armv5(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=cpu:arm926
        :avocado: tags=machine:versatilepb
        :avocado: tags=tuxboot:armv5
        :avocado: tags=image:zImage
        :avocado: tags=console:ttyAMA0
        :avocado: tags=shutdown:nowait
        """
        sums = { "rootfs.ext4.zst" :
                 "17177afa74e7294da0642861f08c88ca3c836764299a54bf6d1ce276cb9712a5",
                 "versatile-pb.dtb" :
                 "0bc0c0b0858cefd3c32b385c0d66d97142ded29472a496f4f490e42fc7615b25",
                 "zImage" :
                 "c95af2f27647c12265d75e9df44c22ff5228c59855f54aaa70f41ec2842e3a4d" }

        self.common_tuxrun(csums=sums,
                           drive="virtio-blk-pci",
                           dt="versatile-pb.dtb")

    def test_armv7(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=cpu:cortex-a15
        :avocado: tags=machine:virt
        :avocado: tags=tuxboot:armv7
        :avocado: tags=image:zImage
        :avocado: tags=console:ttyAMA0
        :avocado: tags=shutdown:nowait
        """
        sums = { "rootfs.ext4.zst" :
                 "ab1fbbeaddda1ffdd45c9405a28cd5370c20f23a7cbc809cc90dc9f243a8eb5a",
                 "zImage" :
                 "4c7a22e9f15875bec06bd2a29d822496571eb297d4f22694099ffcdb19077572" }

        self.common_tuxrun(csums=sums)

    def test_armv7be(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=cpu:cortex-a15
        :avocado: tags=endian:big
        :avocado: tags=machine:virt
        :avocado: tags=tuxboot:armv7be
        :avocado: tags=image:zImage
        :avocado: tags=console:ttyAMA0
        :avocado: tags=shutdown:nowait
        """
        sums = {"rootfs.ext4.zst" :
                "42ed46dd2d59986206c5b1f6cf35eab58fe3fd20c96b41aaa16b32f3f90a9835",
                "zImage" :
                "7facc62082b57af12015b08f7fdbaf2f123ba07a478367853ae12b219afc9f2f" }

        self.common_tuxrun(csums=sums)

    def test_i386(self):
        """
        :avocado: tags=arch:i386
        :avocado: tags=cpu:coreduo
        :avocado: tags=machine:q35
        :avocado: tags=tuxboot:i386
        :avocado: tags=image:bzImage
        :avocado: tags=shutdown:nowait
        """
        sums = {"bzImage" :
                "a3e5b32a354729e65910f5a1ffcda7c14a6c12a55e8213fb86e277f1b76ed956",
                "rootfs.ext4.zst" :
                "f15e66b2bf673a210ec2a4b2e744a80530b36289e04f5388aab812b97f69754a" }

        self.common_tuxrun(csums=sums, drive="virtio-blk-pci")

    def test_mips32(self):
        """
        :avocado: tags=arch:mips
        :avocado: tags=machine:malta
        :avocado: tags=cpu:mips32r6-generic
        :avocado: tags=endian:big
        :avocado: tags=tuxboot:mips32
        :avocado: tags=image:vmlinux
        :avocado: tags=root:sda
        :avocado: tags=shutdown:nowait
        """
        sums = { "rootfs.ext4.zst" :
                 "fc3da0b4c2f38d74c6d705123bb0f633c76ed953128f9d0859378c328a6d11a0",
                 "vmlinux" :
                 "bfd2172f8b17fb32970ca0c8c58f59c5a4ca38aa5855d920be3a69b5d16e52f0" }

        self.common_tuxrun(csums=sums, drive="driver=ide-hd,bus=ide.0,unit=0")

    def test_mips32el(self):
        """
        :avocado: tags=arch:mipsel
        :avocado: tags=machine:malta
        :avocado: tags=cpu:mips32r6-generic
        :avocado: tags=tuxboot:mips32el
        :avocado: tags=image:vmlinux
        :avocado: tags=root:sda
        :avocado: tags=shutdown:nowait
        """
        sums = { "rootfs.ext4.zst" :
                 "e799768e289fd69209c21f4dacffa11baea7543d5db101e8ce27e3bc2c41d90e",
                 "vmlinux" :
                 "8573867c68a8443db8de6d08bb33fb291c189ca2ca671471d3973a3e712096a3" }

        self.common_tuxrun(csums=sums, drive="driver=ide-hd,bus=ide.0,unit=0")

    def test_mips64(self):
        """
        :avocado: tags=arch:mips64
        :avocado: tags=machine:malta
        :avocado: tags=tuxboot:mips64
        :avocado: tags=endian:big
        :avocado: tags=image:vmlinux
        :avocado: tags=root:sda
        :avocado: tags=shutdown:nowait
        """
        sums = { "rootfs.ext4.zst" :
                 "69d91eeb04df3d8d172922c6993bb37d4deeb6496def75d8580f6f9de3e431da",
                 "vmlinux" :
                 "09010e51e4b8bcbbd2494786ffb48eca78f228e96e5c5438344b0eac4029dc61" }

        self.common_tuxrun(csums=sums, drive="driver=ide-hd,bus=ide.0,unit=0")

    def test_mips64el(self):
        """
        :avocado: tags=arch:mips64el
        :avocado: tags=machine:malta
        :avocado: tags=tuxboot:mips64el
        :avocado: tags=image:vmlinux
        :avocado: tags=root:sda
        :avocado: tags=shutdown:nowait
        """
        sums = { "rootfs.ext4.zst" :
                 "fba585368f5915b1498ed081863474b2d7ec4e97cdd46d21bdcb2f9698f83de4",
                 "vmlinux" :
                 "d4e08965e2155c4cccce7c5f34d18fe34c636cda2f2c9844387d614950155266" }

        self.common_tuxrun(csums=sums, drive="driver=ide-hd,bus=ide.0,unit=0")

    def test_ppc32(self):
        """
        :avocado: tags=arch:ppc
        :avocado: tags=machine:ppce500
        :avocado: tags=cpu:e500mc
        :avocado: tags=tuxboot:ppc32
        :avocado: tags=image:uImage
        :avocado: tags=shutdown:nowait
        """
        sums = { "rootfs.ext4.zst" :
                 "8885b9d999cc24d679542a02e9b6aaf48f718f2050ece6b8347074b6ee41dd09",
                 "uImage" :
                 "1a68f74b860fda022fb12e03c5efece8c2b8b590d96cca37a8481a3ae0b3f81f" }

        self.common_tuxrun(csums=sums, drive="virtio-blk-pci")

    def test_ppc64(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=cpu:POWER10
        :avocado: tags=endian:big
        :avocado: tags=console:hvc0
        :avocado: tags=tuxboot:ppc64
        :avocado: tags=image:vmlinux
        :avocado: tags=extradev:driver=spapr-vscsi
        :avocado: tags=root:sda
        """
        sums = { "rootfs.ext4.zst" :
                 "1d953e81a4379e537fc8e41e05a0a59d9b453eef97aa03d47866c6c45b00bdff",
                 "vmlinux" :
                 "f22a9b9e924174a4c199f4c7e5d91a2339fcfe51c6eafd0907dc3e09b64ab728" }
        self.ppc64_common_tuxrun(sums, prefix='tuxrun_ppc64_')

    def test_ppc64le(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=cpu:POWER10
        :avocado: tags=console:hvc0
        :avocado: tags=tuxboot:ppc64le
        :avocado: tags=image:vmlinux
        :avocado: tags=extradev:driver=spapr-vscsi
        :avocado: tags=root:sda
        """
        sums = { "rootfs.ext4.zst" :
                 "b442678c93fb8abe1f7d3bfa20556488de6b475c22c8fed363f42cf81a0a3906",
                 "vmlinux" :
                 "979eb61b445a010fb13e2b927126991f8ceef9c590fa2be0996c00e293e80cf2" }
        self.ppc64_common_tuxrun(sums, prefix='tuxrun_ppc64le_')

    def test_riscv32(self):
        """
        :avocado: tags=arch:riscv32
        :avocado: tags=machine:virt
        :avocado: tags=tuxboot:riscv32
        """
        sums = { "Image" :
                 "89599407d7334de629a40e7ad6503c73670359eb5f5ae9d686353a3d6deccbd5",
                 "fw_jump.elf" :
                 "f2ef28a0b77826f79d085d3e4aa686f1159b315eff9099a37046b18936676985",
                 "rootfs.ext4.zst" :
                 "7168d296d0283238ea73cd5a775b3dd608e55e04c7b92b76ecce31bb13108cba" }

        self.common_tuxrun(csums=sums)

    def test_riscv64(self):
        """
        :avocado: tags=arch:riscv64
        :avocado: tags=machine:virt
        :avocado: tags=tuxboot:riscv64
        """
        sums = { "Image" :
                 "cd634badc65e52fb63465ec99e309c0de0369f0841b7d9486f9729e119bac25e",
                 "fw_jump.elf" :
                 "6e3373abcab4305fe151b564a4c71110d833c21f2c0a1753b7935459e36aedcf",
                 "rootfs.ext4.zst" :
                 "b18e3a3bdf27be03da0b285e84cb71bf09eca071c3a087b42884b6982ed679eb" }

        self.common_tuxrun(csums=sums)

    def test_riscv32_maxcpu(self):
        """
        :avocado: tags=arch:riscv32
        :avocado: tags=machine:virt
        :avocado: tags=cpu:max
        :avocado: tags=tuxboot:riscv32
        """
        sums = { "Image" :
                 "89599407d7334de629a40e7ad6503c73670359eb5f5ae9d686353a3d6deccbd5",
                 "fw_jump.elf" :
                 "f2ef28a0b77826f79d085d3e4aa686f1159b315eff9099a37046b18936676985",
                 "rootfs.ext4.zst" :
                 "7168d296d0283238ea73cd5a775b3dd608e55e04c7b92b76ecce31bb13108cba" }

        self.common_tuxrun(csums=sums)

    def test_riscv64_maxcpu(self):
        """
        :avocado: tags=arch:riscv64
        :avocado: tags=machine:virt
        :avocado: tags=cpu:max
        :avocado: tags=tuxboot:riscv64
        """
        sums = { "Image" :
                 "cd634badc65e52fb63465ec99e309c0de0369f0841b7d9486f9729e119bac25e",
                 "fw_jump.elf" :
                 "6e3373abcab4305fe151b564a4c71110d833c21f2c0a1753b7935459e36aedcf",
                 "rootfs.ext4.zst" :
                 "b18e3a3bdf27be03da0b285e84cb71bf09eca071c3a087b42884b6982ed679eb" }

        self.common_tuxrun(csums=sums)

    def test_s390(self):
        """
        :avocado: tags=arch:s390x
        :avocado: tags=endian:big
        :avocado: tags=tuxboot:s390
        :avocado: tags=image:bzImage
        :avocado: tags=shutdown:nowait
        """
        sums = { "bzImage" :
                 "0414e98dd1c3dafff8496c9cd9c28a5f8d04553bb5ba37e906a812b48d442ef0",
                 "rootfs.ext4.zst" :
                 "88c37c32276677f873a25ab9ec6247895b8e3e6f8259134de2a616080b8ab3fc" }

        self.common_tuxrun(csums=sums,
                           drive="virtio-blk-ccw",
                           haltmsg="Requesting system halt")

    # Note: some segfaults caused by unaligned userspace access
    @skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is unstable on GitLab')
    def test_sh4(self):
        """
        :avocado: tags=arch:sh4
        :avocado: tags=machine:r2d
        :avocado: tags=cpu:sh7785
        :avocado: tags=tuxboot:sh4
        :avocado: tags=image:zImage
        :avocado: tags=root:sda
        :avocado: tags=console:ttySC1
        :avocado: tags=flaky
        """
        sums = { "rootfs.ext4.zst" :
                 "3592a7a3d5a641e8b9821449e77bc43c9904a56c30d45da0694349cfd86743fd",
                 "zImage" :
                 "29d9b2aba604a0f53a5dc3b5d0f2b8e35d497de1129f8ee5139eb6fdf0db692f" }

        # The test is currently too unstable to do much in userspace
        # so we skip common_tuxrun and do a minimal boot and shutdown.
        (kernel, disk, dtb) = self.fetch_tuxrun_assets(csums=sums)

        # the console comes on the second serial port
        self.prepare_run(kernel, disk,
                         "driver=ide-hd,bus=ide.0,unit=0",
                         console_index=1)
        self.vm.launch()

        self.wait_for_console_pattern("Welcome to TuxTest")
        time.sleep(0.1)
        exec_command(self, 'root')
        time.sleep(0.1)
        exec_command_and_wait_for_pattern(self, 'halt',
                                          "reboot: System halted")

    def test_sparc64(self):
        """
        :avocado: tags=arch:sparc64
        :avocado: tags=tuxboot:sparc64
        :avocado: tags=image:vmlinux
        :avocado: tags=root:sda
        :avocado: tags=shutdown:nowait
        """

        sums = { "rootfs.ext4.zst" :
                 "ad2f1dc436ab51583543d25d2c210cab478645d47078d30d129a66ab0e281d76",
                 "vmlinux" :
                 "e34313e4325ff21deaa3d38a502aa09a373ef62b9bd4d7f8f29388b688225c55" }

        self.common_tuxrun(csums=sums, drive="driver=ide-hd,bus=ide.0,unit=0")

    def test_x86_64(self):
        """
        :avocado: tags=arch:x86_64
        :avocado: tags=machine:q35
        :avocado: tags=cpu:Nehalem
        :avocado: tags=tuxboot:x86_64
        :avocado: tags=image:bzImage
        :avocado: tags=root:sda
        :avocado: tags=shutdown:nowait
        """
        sums = { "bzImage" :
                 "2bc7480a669ee9b6b82500a236aba0c54233debe98cb968268fa230f52f03461",
                 "rootfs.ext4.zst" :
                 "b72ac729769b8f51c6dffb221113c9a063c774dbe1d66af30eb593c4e9999b4b" }

        self.common_tuxrun(csums=sums,
                           drive="driver=ide-hd,bus=ide.0,unit=0")
