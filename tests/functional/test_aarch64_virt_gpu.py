#!/usr/bin/env python3
#
# Functional tests for the various graphics modes we can support.
#
# Copyright (c) 2024, 2025 Linaro Ltd.
#
# Author:
#  Alex Bennée <alex.bennee@linaro.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later

from qemu.machine.machine import VMLaunchFailure

from qemu_test import Asset
from qemu_test import exec_command_and_wait_for_pattern as ec_and_wait
from qemu_test import skipIfMissingCommands

from qemu_test.linuxkernel import LinuxKernelTest

from re import search
from subprocess import check_output, CalledProcessError

class Aarch64VirtGPUMachine(LinuxKernelTest):

    ASSET_VIRT_GPU_KERNEL = Asset(
        'https://fileserver.linaro.org/s/ce5jXBFinPxtEdx/'
        'download?path=%2F&files='
        'Image.6.12.16.aarch64',
        '7888c51c55d37e86bbbdeb5acea9f08c34e6b0f03c1f5b2463285f6a6f6eec8b')

    ASSET_VIRT_GPU_ROOTFS = Asset(
        'https://fileserver.linaro.org/s/ce5jXBFinPxtEdx/'
        'download?path=%2F&files='
        'rootfs.aarch64.ext2.zstd',
        'd45118c899420b7e673f1539a37a35480134b3e36e3a59e2cb69b1781cbb14ef')

    def _launch_virt_gpu(self, gpu_device):

        self.set_machine('virt')
        self.require_accelerator("tcg")

        kernel_path = self.ASSET_VIRT_GPU_KERNEL.fetch()
        image_path = self.uncompress(self.ASSET_VIRT_GPU_ROOTFS, format="zstd")

        self.vm.set_console()
        kernel_command_line = (self.KERNEL_COMMON_COMMAND_LINE +
                               'console=ttyAMA0 root=/dev/vda')

        self.vm.add_args("-accel", "tcg")
        self.vm.add_args("-cpu", "cortex-a72")
        self.vm.add_args("-machine", "virt,gic-version=max",
                         '-kernel', kernel_path,
                         '-append', kernel_command_line)
        self.vm.add_args("-smp", "2", "-m", "2048")
        self.vm.add_args("-device", gpu_device)
        self.vm.add_args("-display", "egl-headless")
        self.vm.add_args("-display", "dbus,gl=on")

        self.vm.add_args("-device", "virtio-blk-device,drive=hd0")
        self.vm.add_args("-blockdev",
                         "driver=raw,file.driver=file,"
                         "node-name=hd0,read-only=on,"
                         f"file.filename={image_path}")
        self.vm.add_args("-snapshot")

        try:
            self.vm.launch()
        except VMLaunchFailure as excp:
            if "old virglrenderer, blob resources unsupported" in excp.output:
                self.skipTest("No blob support for virtio-gpu")
            elif "old virglrenderer, venus unsupported" in excp.output:
                self.skipTest("No venus support for virtio-gpu")
            elif "egl: no drm render node available" in excp.output:
                self.skipTest("Can't access host DRM render node")
            elif "'type' does not accept value 'egl-headless'" in excp.output:
                self.skipTest("egl-headless support is not available")
            elif "'type' does not accept value 'dbus'" in excp.output:
                self.skipTest("dbus display support is not available")
            else:
                self.log.info("unhandled launch failure: %s", excp.output)
                raise excp

        self.wait_for_console_pattern('buildroot login:')
        ec_and_wait(self, 'root', '#')

    def _run_virt_weston_test(self, cmd, fail = None):

        # make it easier to detect successful return to shell
        PS1 = 'RES=[$?] # '
        OK_CMD = 'RES=[0] # '

        ec_and_wait(self, 'export XDG_RUNTIME_DIR=/tmp', '#')
        ec_and_wait(self, f"export PS1='{PS1}'", OK_CMD)
        full_cmd = f"weston -B headless --renderer gl --shell kiosk -- {cmd}"
        ec_and_wait(self, full_cmd, OK_CMD, fail)

    @skipIfMissingCommands('zstd')
    def test_aarch64_virt_with_virgl_gpu(self):

        self.require_device('virtio-gpu-gl-pci')

        self._launch_virt_gpu("virtio-gpu-gl-pci")

        # subset of the glmark tests
        tests = " ".join([f"-b {test}" for test in
                          ["build", "texture", "shading",
                           "bump", "desktop", "buffer"]])

        self._run_virt_weston_test("glmark2-wayland --validate " + tests)

    @skipIfMissingCommands('zstd')
    def test_aarch64_virt_with_virgl_blobs_gpu(self):

        self.require_device('virtio-gpu-gl-pci')

        self._launch_virt_gpu("virtio-gpu-gl-pci,hostmem=4G,blob=on")
        self._run_virt_weston_test("glmark2-wayland -b:duration=1.0")

    @skipIfMissingCommands('zstd')
    @skipIfMissingCommands('vulkaninfo')
    def test_aarch64_virt_with_vulkan_gpu(self):

        self.require_device('virtio-gpu-gl-pci')

        try:
            vk_info = check_output(["vulkaninfo", "--summary"],
                                   encoding="utf-8")
        except CalledProcessError as excp:
            self.skipTest(f"Miss-configured host Vulkan: {excp.output}")

        if search(r"driverID\s+=\s+DRIVER_ID_NVIDIA_PROPRIETARY", vk_info):
            self.skipTest("Test skipped on NVIDIA proprietary driver")

        self._launch_virt_gpu("virtio-gpu-gl-pci,hostmem=4G,blob=on,venus=on")
        self._run_virt_weston_test("vkmark -b:duration=1.0",
                                   "debug: stuck in fence wait with iter at")


if __name__ == '__main__':
    LinuxKernelTest.main()
