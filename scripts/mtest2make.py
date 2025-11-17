#! /usr/bin/env python3

# Create Makefile targets to run tests, from Meson's test introspection data.
#
# Author: Paolo Bonzini <pbonzini@redhat.com>

from collections import defaultdict
import itertools
import json
import os
import sys

class Suite(object):
    def __init__(self):
        self.deps = set()
        self.speeds = set()

    def names(self, base):
        return [f'{base}-{speed}' for speed in self.speeds]


print(r'''
SPEED = quick

.speed.quick = $(sort $(filter-out %-slow %-thorough, $1))
.speed.slow = $(sort $(filter-out %-thorough, $1))
.speed.thorough = $(sort $1)

TIMEOUT_MULTIPLIER ?= 1
.mtestargs = --no-rebuild -t $(TIMEOUT_MULTIPLIER)
ifneq ($(SPEED), quick)
.mtestargs += --setup $(SPEED)
endif
.mtestargs += $(subst -j,--num-processes , $(filter-out -j, $(lastword -j1 $(filter -j%, $(MAKEFLAGS)))))

.check.mtestargs = $(MTESTARGS) $(.mtestargs) $(if $(V),--verbose,--print-errorlogs) \
    $(foreach s, $(sort $(.check.mtest-suites)), --suite $s)
.bench.mtestargs = $(MTESTARGS) $(.mtestargs) --benchmark --verbose \
    $(foreach s, $(sort $(.bench.mtest-suites)), --suite $s)''')

introspect = json.load(sys.stdin)

def process_tests(test, targets, suites):
    executable = test['cmd'][0]
    try:
        executable = os.path.relpath(executable)
    except:
        pass

    deps = (targets.get(x, []) for x in test['depends'])
    deps = itertools.chain.from_iterable(deps)
    deps = list(deps)

    test_suites = test['suite'] or ['default']
    for s in test_suites:
        # The suite name in the introspection info is "PROJECT" or "PROJECT:SUITE"
        if ':' in s:
            s = s.split(':')[1]
            if s == 'slow' or s == 'thorough':
                continue
        suites[s].deps.update(deps)
        if s.endswith('-slow'):
            s = s[:-5]
            suites[s].speeds.add('slow')
        if s.endswith('-thorough'):
            s = s[:-9]
            suites[s].speeds.add('thorough')

def emit_prolog(suites, prefix):
    all_targets = ' '.join((f'{prefix}-{k}' for k in suites.keys()))
    all_xml = ' '.join((f'{prefix}-report-{k}.junit.xml' for k in suites.keys()))
    print()
    print(f'all-{prefix}-targets = {all_targets}')
    print(f'all-{prefix}-xml = {all_xml}')
    print(f'.PHONY: {prefix} do-meson-{prefix} {prefix}-report.junit.xml $(all-{prefix}-targets) $(all-{prefix}-xml)')
    print(f'ninja-cmd-goals += $(foreach s, $(.{prefix}.mtest-suites), $(.{prefix}-$s.deps))')
    print(f'{prefix}-build: run-ninja')
    print(f'{prefix} $(all-{prefix}-targets): do-meson-{prefix}')
    print(f'do-meson-{prefix}: run-ninja; $(if $(MAKE.n),,+)$(MESON) test $(.{prefix}.mtestargs)')
    print(f'{prefix}-report.junit.xml $(all-{prefix}-xml): {prefix}-report%.junit.xml: run-ninja')
    print(f'\t$(MAKE) {prefix}$* MTESTARGS="$(MTESTARGS) --logbase {prefix}-report$*" && ln -f meson-logs/$@ .')

def emit_suite(name, suite, prefix):
    deps = ' '.join(suite.deps)
    print()
    print(f'.{prefix}-{name}.deps = {deps}')
    print(f'.ninja-goals.check-build += $(.{prefix}-{name}.deps)')

    names = ' '.join(suite.names(name))
    targets = f'{prefix}-{name} {prefix}-report-{name}.junit.xml'
    if not name.endswith('-slow') and not name.endswith('-thorough'):
        targets += f' {prefix} {prefix}-report.junit.xml'
    print(f'ifneq ($(filter {targets}, $(MAKECMDGOALS)),)')
    # for the "base" suite possibly add FOO-slow and FOO-thorough
    print(f".{prefix}.mtest-suites += {name} $(call .speed.$(SPEED), {names})")
    print(f'endif')

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
