#!/usr/bin/env python3
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
'''
Reverse debugging test for s390x
'''

from reverse_debugging import ReverseDebugging


class ReverseDebuggingS390x(ReverseDebugging):

    def test_revdbg(self):
        self.set_machine('s390-ccw-virtio')
        self.reverse_debugging(gdb_arch='s390:64-bit', shift=6,
                               big_endian=True, args=('-no-shutdown',))


if __name__ == '__main__':
    ReverseDebugging.main()
