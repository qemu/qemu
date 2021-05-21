#!/usr/bin/env python3
#
# Benchmark block jobs
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


import sys
import os
import subprocess
import socket
import json

sys.path.append(os.path.join(os.path.dirname(__file__), '..', '..', 'python'))
from qemu.machine import QEMUMachine
from qemu.qmp import QMPConnectError


def bench_block_job(cmd, cmd_args, qemu_args):
    """Benchmark block-job

    cmd       -- qmp command to run block-job (like blockdev-backup)
    cmd_args  -- dict of qmp command arguments
    qemu_args -- list of Qemu command line arguments, including path to Qemu
                 binary

    Returns {'seconds': int} on success and {'error': str} on failure, dict may
    contain addional 'vm-log' field. Return value is compatible with
    simplebench lib.
    """

    vm = QEMUMachine(qemu_args[0], args=qemu_args[1:])

    try:
        vm.launch()
    except OSError as e:
        return {'error': 'popen failed: ' + str(e)}
    except (QMPConnectError, socket.timeout):
        return {'error': 'qemu failed: ' + str(vm.get_log())}

    try:
        res = vm.qmp(cmd, **cmd_args)
        if res != {'return': {}}:
            vm.shutdown()
            return {'error': '"{}" command failed: {}'.format(cmd, str(res))}

        e = vm.event_wait('JOB_STATUS_CHANGE')
        assert e['data']['status'] == 'created'
        start_ms = e['timestamp']['seconds'] * 1000000 + \
            e['timestamp']['microseconds']

        e = vm.events_wait((('BLOCK_JOB_READY', None),
                            ('BLOCK_JOB_COMPLETED', None),
                            ('BLOCK_JOB_FAILED', None)), timeout=True)
        if e['event'] not in ('BLOCK_JOB_READY', 'BLOCK_JOB_COMPLETED'):
            vm.shutdown()
            return {'error': 'block-job failed: ' + str(e),
                    'vm-log': vm.get_log()}
        if 'error' in e['data']:
            vm.shutdown()
            return {'error': 'block-job failed: ' + e['data']['error'],
                    'vm-log': vm.get_log()}
        end_ms = e['timestamp']['seconds'] * 1000000 + \
            e['timestamp']['microseconds']
    finally:
        vm.shutdown()

    return {'seconds': (end_ms - start_ms) / 1000000.0}


def get_image_size(path):
    out = subprocess.run(['qemu-img', 'info', '--out=json', path],
                         stdout=subprocess.PIPE, check=True).stdout
    return json.loads(out)['virtual-size']


def get_blockdev_size(obj):
    img = obj['filename'] if 'filename' in obj else obj['file']['filename']
    return get_image_size(img)


# Bench backup or mirror
def bench_block_copy(qemu_binary, cmd, cmd_options, source, target):
    """Helper to run bench_block_job() for mirror or backup"""
    assert cmd in ('blockdev-backup', 'blockdev-mirror')

    if target['driver'] == 'qcow2':
        try:
            os.remove(target['file']['filename'])
        except OSError:
            pass

        subprocess.run(['qemu-img', 'create', '-f', 'qcow2',
                        target['file']['filename'],
                        str(get_blockdev_size(source))],
                       stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=True)

    source['node-name'] = 'source'
    target['node-name'] = 'target'

    cmd_options['job-id'] = 'job0'
    cmd_options['device'] = 'source'
    cmd_options['target'] = 'target'
    cmd_options['sync'] = 'full'

    return bench_block_job(cmd, cmd_options,
                           [qemu_binary,
                            '-blockdev', json.dumps(source),
                            '-blockdev', json.dumps(target)])


def drv_file(filename, o_direct=True):
    node = {'driver': 'file', 'filename': filename}
    if o_direct:
        node['cache'] = {'direct': True}
        node['aio'] = 'native'

    return node


def drv_nbd(host, port):
    return {'driver': 'nbd',
            'server': {'type': 'inet', 'host': host, 'port': port}}


def drv_qcow2(file):
    return {'driver': 'qcow2', 'file': file}


if __name__ == '__main__':
    import sys

    if len(sys.argv) < 4:
        print('USAGE: {} <qmp block-job command name> '
              '<json string of arguments for the command> '
              '<qemu binary path and arguments>'.format(sys.argv[0]))
        exit(1)

    res = bench_block_job(sys.argv[1], json.loads(sys.argv[2]), sys.argv[3:])
    if 'seconds' in res:
        print('{:.2f}'.format(res['seconds']))
    else:
        print(res)
