#!/usr/bin/env python3
#
# Functional test that boots a microblaze Linux kernel and checks the console
#
# Copyright (c) 2018, 2021 Red Hat, Inc.
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later. See the COPYING file in the top-level directory.

from test_microblaze_s3adsp1800 import MicroblazeMachine


class MicroblazeLittleEndianMachine(MicroblazeMachine):

    ASSET_IMAGE_LE = MicroblazeMachine.ASSET_IMAGE_LE

    def test_microblaze_s3adsp1800_legacy_le(self):
        self.do_xmaton_le_test('petalogix-s3adsp1800')


if __name__ == '__main__':
    MicroblazeMachine.main()
