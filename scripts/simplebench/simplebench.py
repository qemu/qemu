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

import statistics


def bench_one(test_func, test_env, test_case, count=5, initial_run=True):
    """Benchmark one test-case

    test_func   -- benchmarking function with prototype
                   test_func(env, case), which takes test_env and test_case
                   arguments and on success returns dict with 'seconds' or
                   'iops' (or both) fields, specifying the benchmark result.
                   If both 'iops' and 'seconds' provided, the 'iops' is
                   considered the main, and 'seconds' is just an additional
                   info. On failure test_func should return {'error': str}.
                   Returned dict may contain any other additional fields.
    test_env    -- test environment - opaque first argument for test_func
    test_case   -- test case - opaque second argument for test_func
    count       -- how many times to call test_func, to calculate average
    initial_run -- do initial run of test_func, which don't get into result

    Returns dict with the following fields:
        'runs':     list of test_func results
        'dimension': dimension of results, may be 'seconds' or 'iops'
        'average':  average value (iops or seconds) per run (exists only if at
                    least one run succeeded)
        'stdev':    standard deviation of results
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

    succeeded = [r for r in runs if ('seconds' in r or 'iops' in r)]
    if succeeded:
        if 'iops' in succeeded[0]:
            assert all('iops' in r for r in succeeded)
            dim = 'iops'
        else:
            assert all('seconds' in r for r in succeeded)
            assert all('iops' not in r for r in succeeded)
            dim = 'seconds'
        result['dimension'] = dim
        result['average'] = statistics.mean(r[dim] for r in succeeded)
        result['stdev'] = statistics.stdev(r[dim] for r in succeeded)

    if len(succeeded) < count:
        result['n-failed'] = count - len(succeeded)

    return result


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
