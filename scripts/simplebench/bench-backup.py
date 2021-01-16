#!/usr/bin/env python3
#
# Bench backup block-job
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

import argparse
import json

import simplebench
from results_to_text import results_to_text
from bench_block_job import bench_block_copy, drv_file, drv_nbd


def bench_func(env, case):
    """ Handle one "cell" of benchmarking table. """
    cmd_options = env['cmd-options'] if 'cmd-options' in env else {}
    return bench_block_copy(env['qemu-binary'], env['cmd'],
                            cmd_options,
                            case['source'], case['target'])


def bench(args):
    test_cases = []

    sources = {}
    targets = {}
    for d in args.dir:
        label, path = d.split(':')  # paths with colon not supported
        sources[label] = drv_file(path + '/test-source')
        targets[label] = drv_file(path + '/test-target')

    if args.nbd:
        nbd = args.nbd.split(':')
        host = nbd[0]
        port = '10809' if len(nbd) == 1 else nbd[1]
        drv = drv_nbd(host, port)
        sources['nbd'] = drv
        targets['nbd'] = drv

    for t in args.test:
        src, dst = t.split(':')

        test_cases.append({
            'id': t,
            'source': sources[src],
            'target': targets[dst]
        })

    binaries = []  # list of (<label>, <path>, [<options>])
    for i, q in enumerate(args.env):
        name_path = q.split(':')
        if len(name_path) == 1:
            label = f'q{i}'
            path_opts = name_path[0].split(',')
        else:
            assert len(name_path) == 2  # paths with colon not supported
            label = name_path[0]
            path_opts = name_path[1].split(',')

        binaries.append((label, path_opts[0], path_opts[1:]))

    test_envs = []

    bin_paths = {}
    for i, q in enumerate(args.env):
        opts = q.split(',')
        label_path = opts[0]
        opts = opts[1:]

        if ':' in label_path:
            # path with colon inside is not supported
            label, path = label_path.split(':')
            bin_paths[label] = path
        elif label_path in bin_paths:
            label = label_path
            path = bin_paths[label]
        else:
            path = label_path
            label = f'q{i}'
            bin_paths[label] = path

        x_perf = {}
        is_mirror = False
        for opt in opts:
            if opt == 'mirror':
                is_mirror = True
            elif opt == 'copy-range=on':
                x_perf['use-copy-range'] = True
            elif opt == 'copy-range=off':
                x_perf['use-copy-range'] = False
            elif opt.startswith('max-workers='):
                x_perf['max-workers'] = int(opt.split('=')[1])

        if is_mirror:
            assert not x_perf
            test_envs.append({
                    'id': f'mirror({label})',
                    'cmd': 'blockdev-mirror',
                    'qemu-binary': path
                })
        else:
            test_envs.append({
                'id': f'backup({label})\n' + '\n'.join(opts),
                'cmd': 'blockdev-backup',
                'cmd-options': {'x-perf': x_perf} if x_perf else {},
                'qemu-binary': path
            })

    result = simplebench.bench(bench_func, test_envs, test_cases, count=3)
    with open('results.json', 'w') as f:
        json.dump(result, f, indent=4)
    print(results_to_text(result))


class ExtendAction(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        items = getattr(namespace, self.dest) or []
        items.extend(values)
        setattr(namespace, self.dest, items)


if __name__ == '__main__':
    p = argparse.ArgumentParser('Backup benchmark', epilog='''
ENV format

    (LABEL:PATH|LABEL|PATH)[,max-workers=N][,use-copy-range=(on|off)][,mirror]

    LABEL                short name for the binary
    PATH                 path to the binary
    max-workers          set x-perf.max-workers of backup job
    use-copy-range       set x-perf.use-copy-range of backup job
    mirror               use mirror job instead of backup''',
                                formatter_class=argparse.RawTextHelpFormatter)
    p.add_argument('--env', nargs='+', help='''\
Qemu binaries with labels and options, see below
"ENV format" section''',
                   action=ExtendAction)
    p.add_argument('--dir', nargs='+', help='''\
Directories, each containing "test-source" and/or
"test-target" files, raw images to used in
benchmarking. File path with label, like
label:/path/to/directory''',
                   action=ExtendAction)
    p.add_argument('--nbd', help='''\
host:port for remote NBD image, (or just host, for
default port 10809). Use it in tests, label is "nbd"
(but you cannot create test nbd:nbd).''')
    p.add_argument('--test', nargs='+', help='''\
Tests, in form source-dir-label:target-dir-label''',
                   action=ExtendAction)

    bench(p.parse_args())
