# Class for actually running tests.
#
# Copyright (c) 2020-2021 Virtuozzo International GmbH
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

import os
from pathlib import Path
import datetime
import time
import difflib
import subprocess
import contextlib
import json
import shutil
import sys
from multiprocessing import Pool
from typing import List, Optional, Any, Sequence, Dict
from testenv import TestEnv

if sys.version_info >= (3, 9):
    from contextlib import AbstractContextManager as ContextManager
else:
    from typing import ContextManager


def silent_unlink(path: Path) -> None:
    try:
        path.unlink()
    except OSError:
        pass


def file_diff(file1: str, file2: str) -> List[str]:
    with open(file1, encoding="utf-8") as f1, \
         open(file2, encoding="utf-8") as f2:
        # We want to ignore spaces at line ends. There are a lot of mess about
        # it in iotests.
        # TODO: fix all tests to not produce extra spaces, fix all .out files
        # and use strict diff here!
        seq1 = [line.rstrip() for line in f1]
        seq2 = [line.rstrip() for line in f2]
        res = [line.rstrip()
               for line in difflib.unified_diff(seq1, seq2, file1, file2)]
        return res


class LastElapsedTime(ContextManager['LastElapsedTime']):
    """ Cache for elapsed time for tests, to show it during new test run

    It is safe to use get() at any time.  To use update(), you must either
    use it inside with-block or use save() after update().
    """
    def __init__(self, cache_file: str, env: TestEnv) -> None:
        self.env = env
        self.cache_file = cache_file
        self.cache: Dict[str, Dict[str, Dict[str, float]]]

        try:
            with open(cache_file, encoding="utf-8") as f:
                self.cache = json.load(f)
        except (OSError, ValueError):
            self.cache = {}

    def get(self, test: str,
            default: Optional[float] = None) -> Optional[float]:
        if test not in self.cache:
            return default

        if self.env.imgproto not in self.cache[test]:
            return default

        return self.cache[test][self.env.imgproto].get(self.env.imgfmt,
                                                       default)

    def update(self, test: str, elapsed: float) -> None:
        d = self.cache.setdefault(test, {})
        d.setdefault(self.env.imgproto, {})[self.env.imgfmt] = elapsed

    def save(self) -> None:
        with open(self.cache_file, 'w', encoding="utf-8") as f:
            json.dump(self.cache, f)

    def __enter__(self) -> 'LastElapsedTime':
        return self

    def __exit__(self, exc_type: Any, exc_value: Any, traceback: Any) -> None:
        self.save()


class TestResult:
    def __init__(self, status: str, description: str = '',
                 elapsed: Optional[float] = None, diff: Sequence[str] = (),
                 casenotrun: str = '', interrupted: bool = False) -> None:
        self.status = status
        self.description = description
        self.elapsed = elapsed
        self.diff = diff
        self.casenotrun = casenotrun
        self.interrupted = interrupted


class TestRunner(ContextManager['TestRunner']):
    shared_self = None

    @staticmethod
    def proc_run_test(test: str, test_field_width: int) -> TestResult:
        # We are in a subprocess, we can't change the runner object!
        runner = TestRunner.shared_self
        assert runner is not None
        return runner.run_test(test, test_field_width, mp=True)

    def run_tests_pool(self, tests: List[str],
                       test_field_width: int, jobs: int) -> List[TestResult]:

        # passing self directly to Pool.starmap() just doesn't work, because
        # it's a context manager.
        assert TestRunner.shared_self is None
        TestRunner.shared_self = self

        with Pool(jobs) as p:
            results = p.starmap(self.proc_run_test,
                                zip(tests, [test_field_width] * len(tests)))

        TestRunner.shared_self = None

        return results

    def __init__(self, env: TestEnv, tap: bool = False,
                 color: str = 'auto') -> None:
        self.env = env
        self.tap = tap
        self.last_elapsed = LastElapsedTime('.last-elapsed-cache', env)

        assert color in ('auto', 'on', 'off')
        self.color = (color == 'on') or (color == 'auto' and
                                         sys.stdout.isatty())

        self._stack: contextlib.ExitStack

    def __enter__(self) -> 'TestRunner':
        self._stack = contextlib.ExitStack()
        self._stack.enter_context(self.env)
        self._stack.enter_context(self.last_elapsed)
        return self

    def __exit__(self, exc_type: Any, exc_value: Any, traceback: Any) -> None:
        self._stack.close()

    def test_print_one_line(self, test: str,
                            test_field_width: int,
                            starttime: str,
                            endtime: Optional[str] = None, status: str = '...',
                            lasttime: Optional[float] = None,
                            thistime: Optional[float] = None,
                            description: str = '',
                            end: str = '\n') -> None:
        """ Print short test info before/after test run """
        test = os.path.basename(test)

        if test_field_width is None:
            test_field_width = 8

        if self.tap:
            if status == 'pass':
                print(f'ok {self.env.imgfmt} {test}')
            elif status == 'fail':
                print(f'not ok {self.env.imgfmt} {test}')
            elif status == 'not run':
                print(f'ok {self.env.imgfmt} {test} # SKIP')
            return

        if lasttime:
            lasttime_s = f' (last: {lasttime:.1f}s)'
        else:
            lasttime_s = ''
        if thistime:
            thistime_s = f'{thistime:.1f}s'
        else:
            thistime_s = '...'

        if endtime:
            endtime = f'[{endtime}]'
        else:
            endtime = ''

        if self.color:
            if status == 'pass':
                col = '\033[32m'
            elif status == 'fail':
                col = '\033[1m\033[31m'
            elif status == 'not run':
                col = '\033[33m'
            else:
                col = ''

            col_end = '\033[0m'
        else:
            col = ''
            col_end = ''

        print(f'{test:{test_field_width}} {col}{status:10}{col_end} '
              f'[{starttime}] {endtime:13}{thistime_s:5} {lasttime_s:14} '
              f'{description}', end=end)

    def find_reference(self, test: str) -> str:
        if self.env.cachemode == 'none':
            ref = f'{test}.out.nocache'
            if os.path.isfile(ref):
                return ref

        ref = f'{test}.out.{self.env.imgfmt}'
        if os.path.isfile(ref):
            return ref

        ref = f'{test}.{self.env.qemu_default_machine}.out'
        if os.path.isfile(ref):
            return ref

        return f'{test}.out'

    def do_run_test(self, test: str) -> TestResult:
        """
        Run one test

        :param test: test file path

        Note: this method may be called from subprocess, so it does not
        change ``self`` object in any way!
        """

        f_test = Path(test)
        f_reference = Path(self.find_reference(test))

        if not f_test.exists():
            return TestResult(status='fail',
                              description=f'No such test file: {f_test}')

        if not os.access(str(f_test), os.X_OK):
            sys.exit(f'Not executable: {f_test}')

        if not f_reference.exists():
            return TestResult(status='not run',
                              description='No qualified output '
                                          f'(expected {f_reference})')

        args = [str(f_test.resolve())]
        env = self.env.prepare_subprocess(args)

        # Split test directories, so that tests running in parallel don't
        # break each other.
        for d in ['TEST_DIR', 'SOCK_DIR']:
            env[d] = os.path.join(
                env[d],
                f"{self.env.imgfmt}-{self.env.imgproto}-{f_test.name}")
            Path(env[d]).mkdir(parents=True, exist_ok=True)

        test_dir = env['TEST_DIR']
        f_bad = Path(test_dir, f_test.name + '.out.bad')
        f_notrun = Path(test_dir, f_test.name + '.notrun')
        f_casenotrun = Path(test_dir, f_test.name + '.casenotrun')

        for p in (f_notrun, f_casenotrun):
            silent_unlink(p)

        t0 = time.time()
        with f_bad.open('w', encoding="utf-8") as f:
            with subprocess.Popen(args, cwd=str(f_test.parent), env=env,
                                  stdin=subprocess.DEVNULL,
                                  stdout=f, stderr=subprocess.STDOUT) as proc:
                try:
                    proc.wait()
                except KeyboardInterrupt:
                    proc.terminate()
                    proc.wait()
                    return TestResult(status='not run',
                                      description='Interrupted by user',
                                      interrupted=True)
                ret = proc.returncode

        elapsed = round(time.time() - t0, 1)

        if ret != 0:
            return TestResult(status='fail', elapsed=elapsed,
                              description=f'failed, exit status {ret}',
                              diff=file_diff(str(f_reference), str(f_bad)))

        if f_notrun.exists():
            return TestResult(
                status='not run',
                description=f_notrun.read_text(encoding='utf-8').strip())

        casenotrun = ''
        if f_casenotrun.exists():
            casenotrun = f_casenotrun.read_text(encoding='utf-8')

        diff = file_diff(str(f_reference), str(f_bad))
        if diff:
            if os.environ.get("QEMU_IOTESTS_REGEN", None) is not None:
                shutil.copyfile(str(f_bad), str(f_reference))
                print("########################################")
                print("#####    REFERENCE FILE UPDATED    #####")
                print("########################################")
            return TestResult(status='fail', elapsed=elapsed,
                              description=f'output mismatch (see {f_bad})',
                              diff=diff, casenotrun=casenotrun)
        else:
            f_bad.unlink()
            return TestResult(status='pass', elapsed=elapsed,
                              casenotrun=casenotrun)

    def run_test(self, test: str,
                 test_field_width: int,
                 mp: bool = False) -> TestResult:
        """
        Run one test and print short status

        :param test: test file path
        :param test_field_width: width for first field of status format
        :param mp: if true, we are in a multiprocessing environment, don't try
                   to rewrite things in stdout

        Note: this method may be called from subprocess, so it does not
        change ``self`` object in any way!
        """

        last_el = self.last_elapsed.get(test)
        start = datetime.datetime.now().strftime('%H:%M:%S')

        if not self.tap:
            self.test_print_one_line(test=test,
                                     test_field_width=test_field_width,
                                     status = 'started' if mp else '...',
                                     starttime=start,
                                     lasttime=last_el,
                                     end = '\n' if mp else '\r')
        else:
            testname = os.path.basename(test)
            print(f'# running {self.env.imgfmt} {testname}')

        res = self.do_run_test(test)

        end = datetime.datetime.now().strftime('%H:%M:%S')
        self.test_print_one_line(test=test,
                                 test_field_width=test_field_width,
                                 status=res.status,
                                 starttime=start, endtime=end,
                                 lasttime=last_el, thistime=res.elapsed,
                                 description=res.description)

        if res.casenotrun:
            if self.tap:
                print('#' + res.casenotrun.replace('\n', '\n#'))
            else:
                print(res.casenotrun)

        sys.stdout.flush()
        return res

    def run_tests(self, tests: List[str], jobs: int = 1) -> bool:
        n_run = 0
        failed = []
        notrun = []
        casenotrun = []

        if self.tap:
            print('TAP version 13')
            self.env.print_env('# ')
            print('1..%d' % len(tests))
        else:
            self.env.print_env()

        test_field_width = max(len(os.path.basename(t)) for t in tests) + 2

        if jobs > 1:
            results = self.run_tests_pool(tests, test_field_width, jobs)

        for i, t in enumerate(tests):
            name = os.path.basename(t)

            if jobs > 1:
                res = results[i]
            else:
                res = self.run_test(t, test_field_width)

            assert res.status in ('pass', 'fail', 'not run')

            if res.casenotrun:
                casenotrun.append(t)

            if res.status != 'not run':
                n_run += 1

            if res.status == 'fail':
                failed.append(name)
                if res.diff:
                    if self.tap:
                        print('\n'.join(res.diff), file=sys.stderr)
                    else:
                        print('\n'.join(res.diff))
            elif res.status == 'not run':
                notrun.append(name)
            elif res.status == 'pass':
                assert res.elapsed is not None
                self.last_elapsed.update(t, res.elapsed)

            sys.stdout.flush()
            if res.interrupted:
                break

        if not self.tap:
            if notrun:
                print('Not run:', ' '.join(notrun))

            if casenotrun:
                print('Some cases not run in:', ' '.join(casenotrun))

            if failed:
                print('Failures:', ' '.join(failed))
                print(f'Failed {len(failed)} of {n_run} iotests')
            else:
                print(f'Passed all {n_run} iotests')
        return not failed
