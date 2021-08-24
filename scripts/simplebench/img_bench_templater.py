#!/usr/bin/env python3
#
# Process img-bench test templates
#
# Copyright (c) 2021 Virtuozzo International GmbH.
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
import subprocess
import re
import json

import simplebench
from results_to_text import results_to_text
from table_templater import Templater


def bench_func(env, case):
    test = templater.gen(env['data'], case['data'])

    p = subprocess.run(test, shell=True, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, universal_newlines=True)

    if p.returncode == 0:
        try:
            m = re.search(r'Run completed in (\d+.\d+) seconds.', p.stdout)
            return {'seconds': float(m.group(1))}
        except Exception:
            return {'error': f'failed to parse qemu-img output: {p.stdout}'}
    else:
        return {'error': f'qemu-img failed: {p.returncode}: {p.stdout}'}


if __name__ == '__main__':
    if len(sys.argv) > 1:
        print("""
Usage: img_bench_templater.py < path/to/test-template.sh

This script generates performance tests from a test template (example below),
runs them, and displays the results in a table. The template is read from
stdin.  It must be written in bash and end with a `qemu-img bench` invocation
(whose result is parsed to get the test instance’s result).

Use the following syntax in the template to create the various different test
instances:

  column templating: {var1|var2|...} - test will use different values in
  different columns. You may use several {} constructions in the test, in this
  case product of all choice-sets will be used.

  row templating: [var1|var2|...] - similar thing to define rows (test-cases)

Test template example:

Assume you want to compare two qemu-img binaries, called qemu-img-old and
qemu-img-new in your build directory in two test-cases with 4K writes and 64K
writes. The template may look like this:

qemu_img=/path/to/qemu/build/qemu-img-{old|new}
$qemu_img create -f qcow2 /ssd/x.qcow2 1G
$qemu_img bench -c 100 -d 8 [-s 4K|-s 64K] -w -t none -n /ssd/x.qcow2

When passing this to stdin of img_bench_templater.py, the resulting comparison
table will contain two columns (for two binaries) and two rows (for two
test-cases).

In addition to displaying the results, script also stores results in JSON
format into results.json file in current directory.
""")
        sys.exit()

    templater = Templater(sys.stdin.read())

    envs = [{'id': ' / '.join(x), 'data': x} for x in templater.columns]
    cases = [{'id': ' / '.join(x), 'data': x} for x in templater.rows]

    result = simplebench.bench(bench_func, envs, cases, count=5,
                               initial_run=False)
    print(results_to_text(result))
    with open('results.json', 'w') as f:
        json.dump(result, f, indent=4)
