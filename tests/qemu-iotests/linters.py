# Copyright (C) 2020 Red Hat, Inc.
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

import os
import re
import subprocess
import sys
from typing import List, Mapping, Optional


# TODO: Empty this list!
SKIP_FILES = (
    '030', '040', '041', '044', '045', '055', '056', '057', '065', '093',
    '096', '118', '124', '132', '136', '139', '147', '148', '149',
    '151', '152', '155', '163', '165', '194', '196', '202',
    '203', '205', '206', '207', '208', '210', '211', '212', '213', '216',
    '218', '219', '224', '228', '234', '235', '236', '237', '238',
    '240', '242', '245', '246', '248', '255', '256', '257', '258', '260',
    '262', '264', '266', '274', '277', '280', '281', '295', '296', '298',
    '299', '302', '303', '304', '307',
    'nbd-fault-injector.py', 'qcow2.py', 'qcow2_format.py', 'qed.py'
)


def is_python_file(filename):
    if not os.path.isfile(filename):
        return False

    if filename.endswith('.py'):
        return True

    with open(filename, encoding='utf-8') as f:
        try:
            first_line = f.readline()
            return re.match('^#!.*python', first_line) is not None
        except UnicodeDecodeError:  # Ignore binary files
            return False


def get_test_files() -> List[str]:
    named_tests = [f'tests/{entry}' for entry in os.listdir('tests')]
    check_tests = set(os.listdir('.') + named_tests) - set(SKIP_FILES)
    return list(filter(is_python_file, check_tests))


def run_linter(
        tool: str,
        args: List[str],
        env: Optional[Mapping[str, str]] = None,
        suppress_output: bool = False,
) -> None:
    """
    Run a python-based linting tool.

    :param suppress_output: If True, suppress all stdout/stderr output.
    :raise CalledProcessError: If the linter process exits with failure.
    """
    subprocess.run(
        (sys.executable, '-m', tool, *args),
        env=env,
        check=True,
        stdout=subprocess.PIPE if suppress_output else None,
        stderr=subprocess.STDOUT if suppress_output else None,
        universal_newlines=True,
    )


def main() -> None:
    """
    Used by the Python CI system as an entry point to run these linters.
    """
    def show_usage() -> None:
        print(f"Usage: {sys.argv[0]} < --mypy | --pylint >", file=sys.stderr)
        sys.exit(1)

    if len(sys.argv) != 2:
        show_usage()

    files = get_test_files()

    if sys.argv[1] == '--pylint':
        run_linter('pylint', files)
    elif sys.argv[1] == '--mypy':
        # mypy bug #9852; disable incremental checking as a workaround.
        args = ['--no-incremental'] + files
        run_linter('mypy', args)
    else:
        print(f"Unrecognized argument: '{sys.argv[1]}'", file=sys.stderr)
        show_usage()


if __name__ == '__main__':
    main()
