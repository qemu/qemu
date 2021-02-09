#! /usr/bin/env python3

# Create Makefile targets to run tests, from Meson's test introspection data.
#
# Author: Paolo Bonzini <pbonzini@redhat.com>

from collections import defaultdict
import itertools
import json
import os
import shlex
import sys

class Suite(object):
    def __init__(self):
        self.tests = list()
        self.slow_tests = list()
        self.executables = set()

print('''
SPEED = quick

# $1 = environment, $2 = test command, $3 = test name, $4 = dir
.test-human-tap = $1 $(if $4,(cd $4 && $2),$2) -m $(SPEED) < /dev/null | ./scripts/tap-driver.pl --test-name="$3" $(if $(V),,--show-failures-only)
.test-human-exitcode = $1 $(PYTHON) scripts/test-driver.py $(if $4,-C$4) $(if $(V),--verbose) -- $2 < /dev/null
.test-tap-tap = $1 $(if $4,(cd $4 && $2),$2) < /dev/null | sed "s/^[a-z][a-z]* [0-9]*/& $3/" || true
.test-tap-exitcode = printf "%s\\n" 1..1 "`$1 $(if $4,(cd $4 && $2),$2) < /dev/null > /dev/null || echo "not "`ok 1 $3"
.test.human-print = echo $(if $(V),'$1 $2','Running test $3') &&
.test.env = MALLOC_PERTURB_=$${MALLOC_PERTURB_:-$$(( $${RANDOM:-0} % 255 + 1))}

# $1 = test name, $2 = test target (human or tap)
.test.run = $(call .test.$2-print,$(.test.env.$1),$(.test.cmd.$1),$(.test.name.$1)) $(call .test-$2-$(.test.driver.$1),$(.test.env.$1),$(.test.cmd.$1),$(.test.name.$1),$(.test.dir.$1))

.test.output-format = human
''')

introspect = json.load(sys.stdin)
i = 0

def process_tests(test, targets, suites):
    global i
    env = ' '.join(('%s=%s' % (shlex.quote(k), shlex.quote(v))
                    for k, v in test['env'].items()))
    executable = test['cmd'][0]
    try:
        executable = os.path.relpath(executable)
    except:
        pass
    if test['workdir'] is not None:
        try:
            test['cmd'][0] = os.path.relpath(executable, test['workdir'])
        except:
            test['cmd'][0] = executable
    else:
        test['cmd'][0] = executable
    cmd = ' '.join((shlex.quote(x) for x in test['cmd']))
    driver = test['protocol'] if 'protocol' in test else 'exitcode'

    i += 1
    if test['workdir'] is not None:
        print('.test.dir.%d := %s' % (i, shlex.quote(test['workdir'])))

    deps = (targets.get(x, []) for x in test['depends'])
    deps = itertools.chain.from_iterable(deps)

    print('.test.name.%d := %s' % (i, test['name']))
    print('.test.driver.%d := %s' % (i, driver))
    print('.test.env.%d := $(.test.env) %s' % (i, env))
    print('.test.cmd.%d := %s' % (i, cmd))
    print('.test.deps.%d := %s' % (i, ' '.join(deps)))
    print('.PHONY: run-test-%d' % (i,))
    print('run-test-%d: $(.test.deps.%d)' % (i,i))
    print('\t@$(call .test.run,%d,$(.test.output-format))' % (i,))

    test_suites = test['suite'] or ['default']
    is_slow = any(s.endswith('-slow') for s in test_suites)
    for s in test_suites:
        # The suite name in the introspection info is "PROJECT:SUITE"
        s = s.split(':')[1]
        if s.endswith('-slow'):
            s = s[:-5]
        if is_slow:
            suites[s].slow_tests.append(i)
        else:
            suites[s].tests.append(i)
        suites[s].executables.add(executable)

def emit_prolog(suites, prefix):
    all_tap = ' '.join(('%s-report-%s.tap' % (prefix, k) for k in suites.keys()))
    print('.PHONY: %s %s-report.tap %s' % (prefix, prefix, all_tap))
    print('%s: run-tests' % (prefix,))
    print('%s-report.tap %s: %s-report%%.tap: all' % (prefix, all_tap, prefix))
    print('''\t$(MAKE) .test.output-format=tap --quiet -Otarget V=1 %s$* | ./scripts/tap-merge.pl | tee "$@" \\
              | ./scripts/tap-driver.pl $(if $(V),, --show-failures-only)''' % (prefix, ))

def emit_suite(name, suite, prefix):
    executables = ' '.join(suite.executables)
    slow_test_numbers = ' '.join((str(x) for x in suite.slow_tests))
    test_numbers = ' '.join((str(x) for x in suite.tests))
    target = '%s-%s' % (prefix, name)
    print('.test.quick.%s := %s' % (target, test_numbers))
    print('.test.slow.%s := $(.test.quick.%s) %s' % (target, target, slow_test_numbers))
    print('%s-build: %s' % (prefix, executables))
    print('.PHONY: %s' % (target, ))
    print('.PHONY: %s-report-%s.tap' % (prefix, name))
    print('%s: run-tests' % (target, ))
    print('ifneq ($(filter %s %s, $(MAKECMDGOALS)),)' % (target, prefix))
    print('.tests += $(.test.$(SPEED).%s)' % (target, ))
    print('endif')
    print('all-%s-targets += %s' % (prefix, target))

targets = {t['id']: [os.path.relpath(f) for f in t['filename']]
           for t in introspect['targets']}

testsuites = defaultdict(Suite)
for test in introspect['tests']:
    process_tests(test, targets, testsuites)
emit_prolog(testsuites, 'check')
for name, suite in testsuites.items():
    emit_suite(name, suite, 'check')

benchsuites = defaultdict(Suite)
for test in introspect['benchmarks']:
    process_tests(test, targets, benchsuites)
emit_prolog(benchsuites, 'bench')
for name, suite in benchsuites.items():
    emit_suite(name, suite, 'bench')

print('run-tests: $(patsubst %, run-test-%, $(.tests))')
