#!/usr/bin/env python3
#
# Benchmark preallocate filter
#
# Copyright (c) 2020 Virtuozzo International GmbH.
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
import re
import json

import simplebench
from results_to_text import results_to_text


def qemu_img_bench(args):
    p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       universal_newlines=True)

    if p.returncode == 0:
        try:
            m = re.search(r'Run completed in (\d+.\d+) seconds.', p.stdout)
            return {'seconds': float(m.group(1))}
        except Exception:
            return {'error': f'failed to parse qemu-img output: {p.stdout}'}
    else:
        return {'error': f'qemu-img failed: {p.returncode}: {p.stdout}'}


def bench_func(env, case):
    fname = f"{case['dir']}/prealloc-test.qcow2"
    try:
        os.remove(fname)
    except OSError:
        pass

    subprocess.run([env['qemu-img-binary'], 'create', '-f', 'qcow2', fname,
                   '16G'], stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, check=True)

    args = [env['qemu-img-binary'], 'bench', '-c', str(case['count']),
            '-d', '64', '-s', case['block-size'], '-t', 'none', '-n', '-w']
    if env['prealloc']:
        args += ['--image-opts',
                 'driver=qcow2,file.driver=preallocate,file.file.driver=file,'
                 f'file.file.filename={fname}']
    else:
        args += ['-f', 'qcow2', fname]

    return qemu_img_bench(args)


def auto_count_bench_func(env, case):
    case['count'] = 100
    while True:
        res = bench_func(env, case)
        if 'error' in res:
            return res

        if res['seconds'] >= 1:
            break

        case['count'] *= 10

    if res['seconds'] < 5:
        case['count'] = round(case['count'] * 5 / res['seconds'])
        res = bench_func(env, case)
        if 'error' in res:
            return res

    res['iops'] = case['count'] / res['seconds']
    return res


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f'USAGE: {sys.argv[0]} <qemu-img binary> '
              'DISK_NAME:DIR_PATH ...')
        exit(1)

    qemu_img = sys.argv[1]

    envs = [
        {
            'id': 'no-prealloc',
            'qemu-img-binary': qemu_img,
            'prealloc': False
        },
        {
            'id': 'prealloc',
            'qemu-img-binary': qemu_img,
            'prealloc': True
        }
    ]

    aligned_cases = []
    unaligned_cases = []

    for disk in sys.argv[2:]:
        name, path = disk.split(':')
        aligned_cases.append({
            'id': f'{name}, aligned sequential 16k',
            'block-size': '16k',
            'dir': path
        })
        unaligned_cases.append({
            'id': f'{name}, unaligned sequential 64k',
            'block-size': '16k',
            'dir': path
        })

    result = simplebench.bench(auto_count_bench_func, envs,
                               aligned_cases + unaligned_cases, count=5)
    print(results_to_text(result))
    with open('results.json', 'w') as f:
        json.dump(result, f, indent=4)
