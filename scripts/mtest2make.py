#! /usr/bin/env python3

# Create Makefile targets to run tests, from Meson's test introspection data.
#
# Author: Paolo Bonzini <pbonzini@redhat.com>

from collections import defaultdict
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

# $1 = environment, $2 = test command, $3 = test name
.test-human-tap = $1 $2 < /dev/null | ./scripts/tap-driver.pl --test-name="$3" $(if $(V),,--show-failures-only)
.test-human-exitcode = $1 $2 < /dev/null
.test-tap-tap = $1 $2 < /dev/null | sed "s/^[a-z][a-z]* [0-9]*/& $3/" || true
.test-tap-exitcode = printf "%s\\n" 1..1 "`$1 $2 < /dev/null > /dev/null || echo "not "`ok 1 $3"
.test.print = echo $(if $(V),'$1 $2','Running test $3') >&3
.test.env = MALLOC_PERTURB_=$${MALLOC_PERTURB_:-$$(( $${RANDOM:-0} % 255 + 1))}

# $1 = test name, $2 = test target (human or tap)
.test.run = $(call .test.print,$(.test.env.$1),$(.test.cmd.$1),$(.test.name.$1)) && $(call .test-$2-$(.test.driver.$1),$(.test.env.$1),$(.test.cmd.$1),$(.test.name.$1))

define .test.human_k
        @exec 3>&1; rc=0; $(foreach TEST, $1, $(call .test.run,$(TEST),human) || rc=$$?;) \\
              exit $$rc
endef
define .test.human_no_k
        $(foreach TEST, $1, @exec 3>&1; $(call .test.run,$(TEST),human)
)
endef
.test.human = \\
        $(if $(findstring k, $(MAKEFLAGS)), $(.test.human_k), $(.test.human_no_k))

define .test.tap
        @exec 3>&1; { $(foreach TEST, $1, $(call .test.run,$(TEST),tap); ) } \\
              | ./scripts/tap-merge.pl | tee "$@" \\
              | ./scripts/tap-driver.pl $(if $(V),, --show-failures-only)
endef
''')

suites = defaultdict(Suite)
i = 0
for test in json.load(sys.stdin):
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
    if test['workdir'] is not None:
        cmd = '(cd %s && %s)' % (shlex.quote(test['workdir']), cmd)
    driver = test['protocol'] if 'protocol' in test else 'exitcode'

    i += 1
    print('.test.name.%d := %s' % (i, test['name']))
    print('.test.driver.%d := %s' % (i, driver))
    print('.test.env.%d := $(.test.env) %s' % (i, env))
    print('.test.cmd.%d := %s' % (i, cmd))

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

print('.PHONY: check check-report.tap')
print('check:')
print('check-report.tap:')
print('\t@cat $^ | scripts/tap-merge.pl >$@')
for name, suite in suites.items():
    executables = ' '.join(suite.executables)
    slow_test_numbers = ' '.join((str(x) for x in suite.slow_tests))
    test_numbers = ' '.join((str(x) for x in suite.tests))
    print('.test.suite-quick.%s := %s' % (name, test_numbers))
    print('.test.suite-slow.%s := $(.test.suite-quick.%s) %s' % (name, name, slow_test_numbers))
    print('check-build: %s' % executables)
    print('.PHONY: check-%s' % name)
    print('.PHONY: check-report-%s.tap' % name)
    print('check: check-%s' % name)
    print('check-%s: all %s' % (name, executables))
    print('\t$(call .test.human, $(.test.suite-$(SPEED).%s))' % (name, ))
    print('check-report.tap: check-report-%s.tap' % name)
    print('check-report-%s.tap: %s' % (name, executables))
    print('\t$(call .test.tap, $(.test.suite-$(SPEED).%s))' % (name, ))
