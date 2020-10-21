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


def results_to_text(results):
    """Return text representation of bench() returned dict."""
    from tabulate import tabulate

    dim = None
    tab = [[""] + [c['id'] for c in results['envs']]]
    for case in results['cases']:
        row = [case['id']]
        for env in results['envs']:
            res = results['tab'][case['id']][env['id']]
            if dim is None:
                dim = res['dimension']
            else:
                assert dim == res['dimension']
            row.append(result_to_text(res))
        tab.append(row)

    return f'All results are in {dim}\n\n' + tabulate(tab)
