# Test class for testing the boot process of a Linux kernel
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import hashlib
import urllib.request
import logging
import re
import time

from .cmd import wait_for_console_pattern, exec_command_and_wait_for_pattern
from .testcase import QemuSystemTest
from .utils import get_usernet_hostfwd_port


class LinuxKernelTest(QemuSystemTest):
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    def wait_for_regex_console_pattern(self, success_pattern,
                                       failure_pattern=None,
                                       timeout=None):
        """
        Similar to 'wait_for_console_pattern', but supports regex patterns,
        hence multiple failure/success patterns can be detected at a time.

        Args:
            success_pattern (str | re.Pattern): A regex pattern that indicates
                a successful event. If found, the method exits normally.
            failure_pattern (str | re.Pattern, optional): A regex pattern that
                indicates a failure event. If found, the test fails
            timeout (int, optional): The maximum time (in seconds) to wait for
                a match.
                If exceeded, the test fails.
        """

        console = self.vm.console_file
        console_logger = logging.getLogger('console')

        self.log.debug(
            f"Console interaction: success_msg='{success_pattern}' " +
            f"failure_msg='{failure_pattern}' timeout='{timeout}s'")

        # Only consume console output if waiting for something
        if success_pattern is None and failure_pattern is None:
            return

        start_time = time.time()

        while time.time() - start_time < timeout:
            try:
                msg = console.readline().decode().strip()
            except UnicodeDecodeError:
                msg = None
            if not msg:
                continue
            console_logger.debug(msg)
            if success_pattern is None or re.search(success_pattern, msg):
                break
            if failure_pattern:
                # Find the matching error to print in log
                match = re.search(failure_pattern, msg)
                if not match:
                    continue

                console.close()
                fail = 'Failure message found in console: "%s".' \
                        ' Expected: "%s"' % \
                        (match.group(), success_pattern)
                self.fail(fail)

        if time.time() - start_time >= timeout:
            fail = f"Timeout ({timeout}s) while trying to search pattern"
            self.fail(fail)

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
