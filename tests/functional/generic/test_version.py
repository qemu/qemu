#!/usr/bin/env python3
#
# Version check example test
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.


from qemu_test import QemuSystemTest


class Version(QemuSystemTest):

    def test_qmp_human_info_version(self):
        self.set_machine('none')
        self.vm.add_args('-nodefaults')
        self.vm.launch()
        res = self.vm.cmd('human-monitor-command',
                          command_line='info version')
        self.assertRegex(res, r'^(\d+\.\d+\.\d)')

if __name__ == '__main__':
    QemuSystemTest.main()
