# TestFinder class, define set of tests to run.
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
import glob
import re
from collections import defaultdict
from contextlib import contextmanager
from typing import Optional, List, Iterator, Set


@contextmanager
def chdir(path: Optional[str] = None) -> Iterator[None]:
    if path is None:
        yield
        return

    saved_dir = os.getcwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(saved_dir)


class TestFinder:
    def __init__(self, test_dir: Optional[str] = None) -> None:
        self.groups = defaultdict(set)

        with chdir(test_dir):
            self.all_tests = glob.glob('[0-9][0-9][0-9]')
            self.all_tests += [f for f in glob.iglob('tests/*')
                               if not f.endswith('.out') and
                               os.path.isfile(f + '.out')]

            for t in self.all_tests:
                with open(t, encoding="utf-8") as f:
                    for line in f:
                        if line.startswith('# group: '):
                            for g in line.split()[2:]:
                                self.groups[g].add(t)
                            break

    def add_group_file(self, fname: str) -> None:
        with open(fname, encoding="utf-8") as f:
            for line in f:
                line = line.strip()

                if (not line) or line[0] == '#':
                    continue

                words = line.split()
                test_file = self.parse_test_name(words[0])
                groups = words[1:]

                for g in groups:
                    self.groups[g].add(test_file)

    def parse_test_name(self, name: str) -> str:
        if '/' in name:
            raise ValueError('Paths are unsupported for test selection, '
                             f'requiring "{name}" is wrong')

        if re.fullmatch(r'\d+', name):
            # Numbered tests are old naming convention. We should convert them
            # to three-digit-length, like 1 --> 001.
            name = f'{int(name):03}'
        else:
            # Named tests all should be in tests/ subdirectory
            name = os.path.join('tests', name)

        if name not in self.all_tests:
            raise ValueError(f'Test "{name}" is not found')

        return name

    def find_tests(self, groups: Optional[List[str]] = None,
                   exclude_groups: Optional[List[str]] = None,
                   tests: Optional[List[str]] = None,
                   start_from: Optional[str] = None) -> List[str]:
        """Find tests

        Algorithm:

        1. a. if some @groups specified
             a.1 Take all tests from @groups
             a.2 Drop tests, which are in at least one of @exclude_groups or in
                 'disabled' group (if 'disabled' is not listed in @groups)
             a.3 Add tests from @tests (don't exclude anything from them)

           b. else, if some @tests specified:
             b.1 exclude_groups must be not specified, so just take @tests

           c. else (only @exclude_groups list is non-empty):
             c.1 Take all tests
             c.2 Drop tests, which are in at least one of @exclude_groups or in
                 'disabled' group

        2. sort

        3. If start_from specified, drop tests from first one to @start_from
           (not inclusive)
        """
        if groups is None:
            groups = []
        if exclude_groups is None:
            exclude_groups = []
        if tests is None:
            tests = []

        res: Set[str] = set()
        if groups:
            # Some groups specified. exclude_groups supported, additionally
            # selecting some individual tests supported as well.
            res.update(*(self.groups[g] for g in groups))
        elif tests:
            # Some individual tests specified, but no groups. In this case
            # we don't support exclude_groups.
            if exclude_groups:
                raise ValueError("Can't exclude from individually specified "
                                 "tests.")
        else:
            # No tests no groups: start from all tests, exclude_groups
            # supported.
            res.update(self.all_tests)

        if 'disabled' not in groups and 'disabled' not in exclude_groups:
            # Don't want to modify function argument, so create new list.
            exclude_groups = exclude_groups + ['disabled']

        res = res.difference(*(self.groups[g] for g in exclude_groups))

        # We want to add @tests. But for compatibility with old test names,
        # we should convert any number < 100 to number padded by
        # leading zeroes, like 1 -> 001 and 23 -> 023.
        for t in tests:
            res.add(self.parse_test_name(t))

        sequence = sorted(res)

        if start_from is not None:
            del sequence[:sequence.index(self.parse_test_name(start_from))]

        return sequence
