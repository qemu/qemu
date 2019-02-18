# Linux initrd acceptance test.
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Wainer dos Santos Moschetta <wainersm@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.

import tempfile
from avocado.utils.process import run

from avocado_qemu import Test


class LinuxInitrd(Test):
    """
    Checks QEMU evaluates correctly the initrd file passed as -initrd option.

    :avocado: tags=x86_64
    """

    timeout = 60

    def test_with_2gib_file_should_exit_error_msg(self):
        """
        Pretends to boot QEMU with an initrd file with size of 2GiB
        and expect it exits with error message.
        """
        kernel_url = ('https://mirrors.kernel.org/fedora/releases/28/'
                      'Everything/x86_64/os/images/pxeboot/vmlinuz')
        kernel_hash = '238e083e114c48200f80d889f7e32eeb2793e02a'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)
        max_size = 2 * (1024 ** 3) - 1

        with tempfile.NamedTemporaryFile() as initrd:
            initrd.seek(max_size)
            initrd.write(b'\0')
            initrd.flush()
            cmd = "%s -kernel %s -initrd %s" % (self.qemu_bin, kernel_path,
                                                initrd.name)
            res = run(cmd, ignore_status=True)
            self.assertEqual(res.exit_status, 1)
            expected_msg = r'.*initrd is too large.*max: \d+, need %s.*' % (
                max_size + 1)
            self.assertRegex(res.stderr_text, expected_msg)
