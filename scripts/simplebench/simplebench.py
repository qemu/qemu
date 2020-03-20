#!/usr/bin/env python
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


def bench_one(test_func, test_env, test_case, count=5, initial_run=True):
    """Benchmark one test-case

    test_func   -- benchmarking function with prototype
                   test_func(env, case), which takes test_env and test_case
                   arguments and returns {'seconds': int} (which is benchmark
                   result) on success and {'error': str} on error. Returned
                   dict may contain any other additional fields.
    test_env    -- test environment - opaque first argument for test_func
    test_case   -- test case - opaque second argument for test_func
    count       -- how many times to call test_func, to calculate average
    initial_run -- do initial run of test_func, which don't get into result

    Returns dict with the following fields:
        'runs':     list of test_func results
        'average':  average seconds per run (exists only if at least one run
                    succeeded)
        'delta':    maximum delta between test_func result and the average
                    (exists only if at least one run succeeded)
        'n-failed': number of failed runs (exists only if at least one run
                    failed)
    """
    if initial_run:
        print('  #initial run:')
        print('   ', test_func(test_env, test_case))

    runs = []
    for i in range(count):
        print('  #run {}'.format(i+1))
        res = test_func(test_env, test_case)
        print('   ', res)
        runs.append(res)

    result = {'runs': runs}

    successed = [r for r in runs if ('seconds' in r)]
    if successed:
        avg = sum(r['seconds'] for r in successed) / len(successed)
        result['average'] = avg
        result['delta'] = max(abs(r['seconds'] - avg) for r in successed)

    if len(successed) < count:
        result['n-failed'] = count - len(successed)

    return result


def ascii_one(result):
    """Return ASCII representation of bench_one() returned dict."""
    if 'average' in result:
        s = '{:.2f} +- {:.2f}'.format(result['average'], result['delta'])
        if 'n-failed' in result:
            s += '\n({} failed)'.format(result['n-failed'])
        return s
    else:
        return 'FAILED'


def bench(test_func, test_envs, test_cases, *args, **vargs):
    """Fill benchmark table

    test_func -- benchmarking function, see bench_one for description
    test_envs -- list of test environments, see bench_one
    test_cases -- list of test cases, see bench_one
    args, vargs -- additional arguments for bench_one

    Returns dict with the following fields:
        'envs':  test_envs
        'cases': test_cases
        'tab':   filled 2D array, where cell [i][j] is bench_one result for
                 test_cases[i] for test_envs[j] (i.e., rows are test cases and
                 columns are test environments)
    """
    tab = {}
    results = {
        'envs': test_envs,
        'cases': test_cases,
        'tab': tab
    }
    n = 1
    n_tests = len(test_envs) * len(test_cases)
    for env in test_envs:
        for case in test_cases:
            print('Testing {}/{}: {} :: {}'.format(n, n_tests,
                                                   env['id'], case['id']))
            if case['id'] not in tab:
                tab[case['id']] = {}
            tab[case['id']][env['id']] = bench_one(test_func, env, case,
                                                   *args, **vargs)
            n += 1

    print('Done')
    return results


def ascii(results):
    """Return ASCII representation of bench() returned dict."""
    from tabulate import tabulate

    tab = [[""] + [c['id'] for c in results['envs']]]
    for case in results['cases']:
        row = [case['id']]
        for env in results['envs']:
            row.append(ascii_one(results['tab'][case['id']][env['id']]))
        tab.append(row)

    return tabulate(tab)
