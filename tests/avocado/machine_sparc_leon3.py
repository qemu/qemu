# Functional test that boots a Leon3 machine and checks its serial console.
#
# Copyright (c) Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

from avocado_qemu import Test
from avocado_qemu import wait_for_console_pattern
from avocado import skip


class Leon3Machine(Test):

    timeout = 60

    @skip("Test currently broken")
    # A Window Underflow exception occurs before booting the kernel,
    # and QEMU exit calling cpu_abort(), which makes this test to fail.
    def test_leon3_helenos_uimage(self):
        """
        :avocado: tags=arch:sparc
        :avocado: tags=machine:leon3_generic
        :avocado: tags=binfmt:uimage
        """
        kernel_url = ('http://www.helenos.org/releases/'
                      'HelenOS-0.6.0-sparc32-leon3.bin')
        kernel_hash = 'a88c9cfdb8430c66650e5290a08765f9bf049a30'
        kernel_path = self.fetch_asset(kernel_url, asset_hash=kernel_hash)

        self.vm.set_console()
        self.vm.add_args('-kernel', kernel_path)

        self.vm.launch()

        wait_for_console_pattern(self, 'Copyright (c) 2001-2014 HelenOS project')
        wait_for_console_pattern(self, 'Booting the kernel ...')
