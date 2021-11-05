# Version check example test
#
# Copyright (c) 2018 Red Hat, Inc.
#
# Author:
#  Cleber Rosa <crosa@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.


from avocado_qemu import Test


class Version(Test):
    """
    :avocado: tags=quick
    """
    def test_qmp_human_info_version(self):
        self.vm.add_args('-nodefaults')
        self.vm.launch()
        res = self.vm.command('human-monitor-command',
                              command_line='info version')
        self.assertRegexpMatches(res, r'^(\d+\.\d+\.\d)')
