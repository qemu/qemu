#!/usr/bin/env python3
#
# Ubuntu VM testing library
#
# Copyright 2017 Red Hat Inc.
# Copyright 2020 Linaro
#
# Authors:
#  Robert Foley <robert.foley@linaro.org>
#  Originally based on ubuntu.i386 Fam Zheng <famz@redhat.com>
#
# This code is licensed under the GPL version 2 or later.  See
# the COPYING file in the top-level directory.

import os
import subprocess
import basevm

class UbuntuVM(basevm.BaseVM):

    def __init__(self, args, config=None):
        self.login_prompt = "ubuntu-{}-guest login:".format(self.arch)
        basevm.BaseVM.__init__(self, args, config)

    def build_image(self, img):
        """Build an Ubuntu VM image.  The child class will
           define the install_cmds to init the VM."""
        os_img = self._download_with_cache(self.image_link,
                                           sha256sum=self.image_sha256)
        img_tmp = img + ".tmp"
        subprocess.check_call(["cp", "-f", os_img, img_tmp])
        self.exec_qemu_img("resize", img_tmp, "+50G")
        ci_img = self.gen_cloud_init_iso()

        self.boot(img_tmp, extra_args = [ "-device", "VGA", "-cdrom", ci_img, ])

        # First command we issue is fix for slow ssh login.
        self.wait_ssh(wait_root=True,
                      cmd="chmod -x /etc/update-motd.d/*")
        # Wait for cloud init to finish
        self.wait_ssh(wait_root=True,
                      cmd="ls /var/lib/cloud/instance/boot-finished")
        self.ssh_root("touch /etc/cloud/cloud-init.disabled")
        # Disable auto upgrades.
        # We want to keep the VM system state stable.
        self.ssh_root('sed -ie \'s/"1"/"0"/g\' '\
                      '/etc/apt/apt.conf.d/20auto-upgrades')
        self.ssh_root("sed -ie s/^#\ deb-src/deb-src/g /etc/apt/sources.list")

        # If the user chooses not to do the install phase,
        # then we will jump right to the graceful shutdown
        if self._config['install_cmds'] != "":
            # Issue the install commands.
            # This can be overridden by the user in the config .yml.
            install_cmds = self._config['install_cmds'].split(',')
            for cmd in install_cmds:
                self.ssh_root(cmd)
        self.graceful_shutdown()
        os.rename(img_tmp, img)
        return 0
