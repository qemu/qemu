#!/usr/bin/env python3
#
# Simple benchmarking framework
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

import math
import tabulate

# We want leading whitespace for difference row cells (see below)
tabulate.PRESERVE_WHITESPACE = True


def format_value(x, stdev):
    stdev_pr = stdev / x * 100
    if stdev_pr < 1.5:
        # don't care too much
        return f'{x:.2g}'
    else:
        return f'{x:.2g} ± {math.ceil(stdev_pr)}%'


def result_to_text(result):
    """Return text representation of bench_one() returned dict."""
    if 'average' in result:
        s = format_value(result['average'], result['stdev'])
        if 'n-failed' in result:
            s += '\n({} failed)'.format(result['n-failed'])
        return s
    else:
        return 'FAILED'


def results_dimension(results):
    dim = None
    for case in results['cases']:
        for env in results['envs']:
            res = results['tab'][case['id']][env['id']]
            if dim is None:
                dim = res['dimension']
            else:
                assert dim == res['dimension']

    assert dim in ('iops', 'seconds')

    return dim


def results_to_text(results):
    """Return text representation of bench() returned dict."""
    n_columns = len(results['envs'])
    named_columns = n_columns > 2
    dim = results_dimension(results)
    tab = []

    if named_columns:
        # Environment columns are named A, B, ...
        tab.append([''] + [chr(ord('A') + i) for i in range(n_columns)])

    tab.append([''] + [c['id'] for c in results['envs']])

    for case in results['cases']:
        row = [case['id']]
        case_results = results['tab'][case['id']]
        for env in results['envs']:
            res = case_results[env['id']]
            row.append(result_to_text(res))
        tab.append(row)

        # Add row of difference between columns. For each column starting from
        # B we calculate difference with all previous columns.
        row = ['', '']  # case name and first column
        for i in range(1, n_columns):
            cell = ''
            env = results['envs'][i]
            res = case_results[env['id']]

            if 'average' not in res:
                # Failed result
                row.append(cell)
                continue

            for j in range(0, i):
                env_j = results['envs'][j]
                res_j = case_results[env_j['id']]
                cell += ' '

                if 'average' not in res_j:
                    # Failed result
                    cell += '--'
                    continue

                col_j = tab[0][j + 1] if named_columns else ''
                diff_pr = round((res['average'] - res_j['average']) /
                                res_j['average'] * 100)
                cell += f' {col_j}{diff_pr:+}%'
            row.append(cell)
        tab.append(row)

    return f'All results are in {dim}\n\n' + tabulate.tabulate(tab)


if __name__ == '__main__':
    import sys
    import json

    if len(sys.argv) < 2:
        print(f'USAGE: {sys.argv[0]} results.json')
        exit(1)

    with open(sys.argv[1]) as f:
        print(results_to_text(json.load(f)))
