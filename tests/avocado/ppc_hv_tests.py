# Tests that specifically try to exercise hypervisor features of the
# target machines. powernv supports the Power hypervisor ISA, and
# pseries supports the nested-HV hypervisor spec.
#
# Copyright (c) 2023 IBM Corporation
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

from avocado import skipIf, skipUnless
from avocado.utils import archive
from avocado_qemu import QemuSystemTest
from avocado_qemu import wait_for_console_pattern, exec_command
import os
import time
import subprocess

deps = ["xorriso"] # dependent tools needed in the test setup/box.

def which(tool):
    """ looks up the full path for @tool, returns None if not found
        or if @tool does not have executable permissions.
    """
    paths=os.getenv('PATH')
    for p in paths.split(os.path.pathsep):
        p = os.path.join(p, tool)
        if os.path.exists(p) and os.access(p, os.X_OK):
            return p
    return None

def missing_deps():
    """ returns True if any of the test dependent tools are absent.
    """
    for dep in deps:
        if which(dep) is None:
            return True
    return False

# Alpine is a light weight distro that supports QEMU. These tests boot
# that on the machine then run a QEMU guest inside it in KVM mode,
# that runs the same Alpine distro image.
# QEMU packages are downloaded and installed on each test. That's not a
# large download, but it may be more polite to create qcow2 image with
# QEMU already installed and use that.
@skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test sometimes gets stuck due to console handling problem')
@skipUnless(os.getenv('AVOCADO_ALLOW_LARGE_STORAGE'), 'storage limited')
@skipUnless(os.getenv('SPEED') == 'slow', 'runtime limited')
@skipIf(missing_deps(), 'dependencies (%s) not installed' % ','.join(deps))
class HypervisorTest(QemuSystemTest):

    timeout = 1000
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 console=hvc0 '
    panic_message = 'Kernel panic - not syncing'
    good_message = 'VFS: Cannot open root device'

    def extract_from_iso(self, iso, path):
        """
        Extracts a file from an iso file into the test workdir

        :param iso: path to the iso file
        :param path: path within the iso file of the file to be extracted
        :returns: path of the extracted file
        """
        filename = os.path.basename(path)

        cwd = os.getcwd()
        os.chdir(self.workdir)

        with open(filename, "w") as outfile:
            cmd = "xorriso -osirrox on -indev %s -cpx %s %s" % (iso, path, filename)
            subprocess.run(cmd.split(),
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        os.chdir(cwd)

        # Return complete path to extracted file.  Because callers to
        # extract_from_iso() specify 'path' with a leading slash, it is
        # necessary to use os.path.relpath() as otherwise os.path.join()
        # interprets it as an absolute path and drops the self.workdir part.
        return os.path.normpath(os.path.join(self.workdir, filename))

    def setUp(self):
        super().setUp()

        iso_url = ('https://dl-cdn.alpinelinux.org/alpine/v3.18/releases/ppc64le/alpine-standard-3.18.4-ppc64le.iso')

        # Alpine use sha256 so I recalculated this myself
        iso_sha256 = 'c26b8d3e17c2f3f0fed02b4b1296589c2390e6d5548610099af75300edd7b3ff'
        iso_path = self.fetch_asset(iso_url, asset_hash=iso_sha256,
                                    algorithm = "sha256")

        self.iso_path = iso_path
        self.vmlinuz = self.extract_from_iso(iso_path, '/boot/vmlinuz-lts')
        self.initramfs = self.extract_from_iso(iso_path, '/boot/initramfs-lts')

    def do_start_alpine(self):
        self.vm.set_console()
        kernel_command_line = self.KERNEL_COMMON_COMMAND_LINE
        self.vm.add_args("-kernel", self.vmlinuz)
        self.vm.add_args("-initrd", self.initramfs)
        self.vm.add_args("-smp", "4", "-m", "2g")
        self.vm.add_args("-drive", f"file={self.iso_path},format=raw,if=none,id=drive0")

        self.vm.launch()
        wait_for_console_pattern(self, 'Welcome to Alpine Linux 3.18')
        exec_command(self, 'root')
        wait_for_console_pattern(self, 'localhost login:')
        wait_for_console_pattern(self, 'You may change this message by editing /etc/motd.')
        exec_command(self, 'setup-alpine -qe')
        wait_for_console_pattern(self, 'Updating repository indexes... done.')

    def do_stop_alpine(self):
        exec_command(self, 'poweroff')
        wait_for_console_pattern(self, 'alpine:~#')
        self.vm.wait()

    def do_setup_kvm(self):
        exec_command(self, 'echo http://dl-cdn.alpinelinux.org/alpine/v3.18/main > /etc/apk/repositories')
        wait_for_console_pattern(self, 'alpine:~#')
        exec_command(self, 'echo http://dl-cdn.alpinelinux.org/alpine/v3.18/community >> /etc/apk/repositories')
        wait_for_console_pattern(self, 'alpine:~#')
        exec_command(self, 'apk update')
        wait_for_console_pattern(self, 'alpine:~#')
        exec_command(self, 'apk add qemu-system-ppc64')
        wait_for_console_pattern(self, 'alpine:~#')
        exec_command(self, 'modprobe kvm-hv')
        wait_for_console_pattern(self, 'alpine:~#')

    # This uses the host's block device as the source file for guest block
    # device for install media. This is a bit hacky but allows reuse of the
    # iso without having a passthrough filesystem configured.
    def do_test_kvm(self, hpt=False):
        if hpt:
            append = 'disable_radix'
        else:
            append = ''
        exec_command(self, 'qemu-system-ppc64 -nographic -smp 2 -m 1g '
                           '-machine pseries,x-vof=on,accel=kvm '
                           '-machine cap-cfpc=broken,cap-sbbc=broken,'
                                    'cap-ibs=broken,cap-ccf-assist=off '
                           '-drive file=/dev/nvme0n1,format=raw,readonly=on '
                           '-initrd /media/nvme0n1/boot/initramfs-lts '
                           '-kernel /media/nvme0n1/boot/vmlinuz-lts '
                           '-append \'usbcore.nousb ' + append + '\'')
        # Alpine 3.18 kernel seems to crash in XHCI USB driver.
        wait_for_console_pattern(self, 'Welcome to Alpine Linux 3.18')
        exec_command(self, 'root')
        wait_for_console_pattern(self, 'localhost login:')
        wait_for_console_pattern(self, 'You may change this message by editing /etc/motd.')
        exec_command(self, 'poweroff >& /dev/null')
        wait_for_console_pattern(self, 'localhost:~#')
        wait_for_console_pattern(self, 'reboot: Power down')
        time.sleep(1)
        exec_command(self, '')
        wait_for_console_pattern(self, 'alpine:~#')

    def test_hv_pseries(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=accel:tcg
        """
        self.require_accelerator("tcg")
        self.vm.add_args("-accel", "tcg,thread=multi")
        self.vm.add_args('-device', 'nvme,serial=1234,drive=drive0')
        self.vm.add_args("-machine", "x-vof=on,cap-nested-hv=on")
        self.do_start_alpine()
        self.do_setup_kvm()
        self.do_test_kvm()
        self.do_stop_alpine()

    def test_hv_pseries_kvm(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:pseries
        :avocado: tags=accel:kvm
        """
        self.require_accelerator("kvm")
        self.vm.add_args("-accel", "kvm")
        self.vm.add_args('-device', 'nvme,serial=1234,drive=drive0')
        self.vm.add_args("-machine", "x-vof=on,cap-nested-hv=on,cap-ccf-assist=off")
        self.do_start_alpine()
        self.do_setup_kvm()
        self.do_test_kvm()
        self.do_stop_alpine()

    def test_hv_powernv(self):
        """
        :avocado: tags=arch:ppc64
        :avocado: tags=machine:powernv
        :avocado: tags=accel:tcg
        """
        self.require_accelerator("tcg")
        self.vm.add_args("-accel", "tcg,thread=multi")
        self.vm.add_args('-device', 'nvme,bus=pcie.2,addr=0x0,serial=1234,drive=drive0',
                         '-device', 'e1000e,netdev=net0,mac=C0:FF:EE:00:00:02,bus=pcie.0,addr=0x0',
                         '-netdev', 'user,id=net0,hostfwd=::20022-:22,hostname=alpine')
        self.do_start_alpine()
        self.do_setup_kvm()
        self.do_test_kvm()
        self.do_test_kvm(True)
        self.do_stop_alpine()
