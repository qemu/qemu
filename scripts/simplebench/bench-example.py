#!/usr/bin/env python3
#
# Benchmark example
#
# Copyright (c) 2019 Virtuozzo International GmbH.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

import simplebench
from bench_block_job import bench_block_copy, drv_file, drv_nbd


def bench_func(env, case):
    """ Handle one "cell" of benchmarking table. """
    return bench_block_copy(env['qemu_binary'], env['cmd'],
                            case['source'], case['target'])


# You may set the following five variables to correct values, to turn this
# example to real benchmark.
ssd_source = '/path-to-raw-source-image-at-ssd'
ssd_target = '/path-to-raw-target-image-at-ssd'
hdd_target = '/path-to-raw-source-image-at-hdd'
nbd_ip = 'nbd-ip-addr'
nbd_port = 'nbd-port-number'

# Test-cases are "rows" in benchmark resulting table, 'id' is a caption for
# the row, other fields are handled by bench_func.
test_cases = [
    {
        'id': 'ssd -> ssd',
        'source': drv_file(ssd_source),
        'target': drv_file(ssd_target)
    },
    {
        'id': 'ssd -> hdd',
        'source': drv_file(ssd_source),
        'target': drv_file(hdd_target)
    },
    {
        'id': 'ssd -> nbd',
        'source': drv_file(ssd_source),
        'target': drv_nbd(nbd_ip, nbd_port)
    },
]

# Test-envs are "columns" in benchmark resulting table, 'id is a caption for
# the column, other fields are handled by bench_func.
test_envs = [
    {
        'id': 'backup-1',
        'cmd': 'blockdev-backup',
        'qemu_binary': '/path-to-qemu-binary-1'
    },
    {
        'id': 'backup-2',
        'cmd': 'blockdev-backup',
        'qemu_binary': '/path-to-qemu-binary-2'
    },
    {
        'id': 'mirror',
        'cmd': 'blockdev-mirror',
        'qemu_binary': '/path-to-qemu-binary-1'
    }
]

result = simplebench.bench(bench_func, test_envs, test_cases, count=3)
print(simplebench.ascii(result))
