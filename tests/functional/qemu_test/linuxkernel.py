# Test class for testing the boot process of a Linux kernel
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import os

from .testcase import QemuSystemTest
from .cmd import run_cmd, wait_for_console_pattern
from .utils import archive_extract

class LinuxKernelTest(QemuSystemTest):
    KERNEL_COMMON_COMMAND_LINE = 'printk.time=0 '

    def wait_for_console_pattern(self, success_message, vm=None):
        wait_for_console_pattern(self, success_message,
                                 failure_message='Kernel panic - not syncing',
                                 vm=vm)

    def extract_from_deb(self, deb_path, path):
        """
        Extracts a file from a deb package into the test workdir

        :param deb_path: path to the deb archive
        :param path: path within the deb archive of the file to be extracted
        :returns: path of the extracted file
        """
        cwd = os.getcwd()
        os.chdir(self.workdir)
        (stdout, stderr, ret) = run_cmd(['ar', 't', deb_path])
        file_path = stdout.split()[2]
        run_cmd(['ar', 'x', deb_path, file_path])
        archive_extract(file_path, self.workdir)
        os.chdir(cwd)
        # Return complete path to extracted file.  Because callers to
        # extract_from_deb() specify 'path' with a leading slash, it is
        # necessary to use os.path.relpath() as otherwise os.path.join()
        # interprets it as an absolute path and drops the self.workdir part.
        return os.path.normpath(os.path.join(self.workdir,
                                             os.path.relpath(path, '/')))

