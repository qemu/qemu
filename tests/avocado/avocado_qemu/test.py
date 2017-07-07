# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# See LICENSE for more details.
#
# Copyright (C) 2017 Red Hat Inc
#
# Authors:
#  Amador Pahim <apahim@redhat.com>


from avocado import Test
from .base import VM


class QemuTest(Test):

    def __init__(self, methodName=None, name=None, params=None,
                 base_logdir=None, job=None, runner_queue=None):
        super(QemuTest, self).__init__(methodName=methodName, name=name,
                                       params=params, base_logdir=base_logdir,
                                       job=job, runner_queue=runner_queue)
        self.vm = VM(qemu_bin=self.params.get('qemu_bin'),
                     arch=self.params.get('arch'))
