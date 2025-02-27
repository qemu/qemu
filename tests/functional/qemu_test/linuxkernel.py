# Test class for testing the boot process of a Linux kernel
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import hashlib
import urllib.request

from .cmd import wait_for_console_pattern, exec_command_and_wait_for_pattern
from .testcase import QemuSystemTest
from .utils import get_usernet_hostfwd_port


class LinuxKernelTest(QemuSystemTest):
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    def launch_kernel(self, kernel, initrd=None, dtb=None, console_index=0,
                      wait_for=None):
        self.vm.set_console(console_index=console_index)
        self.vm.add_args('-kernel', kernel)
        if initrd:
                self.vm.add_args('-initrd', initrd)
        if dtb:
                self.vm.add_args('-dtb', dtb)
        self.vm.launch()
        if wait_for:
                self.wait_for_console_pattern(wait_for)

    def check_http_download(self, filename, hashsum, guestport=8080,
                            pythoncmd='python3 -m http.server'):
        exec_command_and_wait_for_pattern(self,
                        f'{pythoncmd} {guestport} & sleep 1',
                        f'Serving HTTP on 0.0.0.0 port {guestport}')
        hl = hashlib.sha256()
        hostport = get_usernet_hostfwd_port(self.vm)
        url = f'http://localhost:{hostport}{filename}'
        self.log.info(f'Downloading {url} ...')
        with urllib.request.urlopen(url) as response:
            while True:
                chunk = response.read(1 << 20)
                if not chunk:
                    break
                hl.update(chunk)

        digest = hl.hexdigest()
        self.log.info(f'sha256sum of download is {digest}.')
        self.assertEqual(digest, hashsum)
