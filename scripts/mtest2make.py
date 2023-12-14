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
        self.deps = set()
        self.speeds = ['quick']

    def names(self, base):
        return [base if speed == 'quick' else f'{base}-{speed}' for speed in self.speeds]


print('''
SPEED = quick

.speed.quick = $(foreach s,$(sort $(filter-out %-slow %-thorough, $1)), --suite $s)
.speed.slow = $(foreach s,$(sort $(filter-out %-thorough, $1)), --suite $s)
.speed.thorough = $(foreach s,$(sort $1), --suite $s)

.mtestargs = --no-rebuild -t 0
ifneq ($(SPEED), quick)
.mtestargs += --setup $(SPEED)
endif
.mtestargs += $(subst -j,--num-processes , $(filter-out -j, $(lastword -j1 $(filter -j%, $(MAKEFLAGS)))))

.check.mtestargs = $(MTESTARGS) $(.mtestargs) $(if $(V),--verbose,--print-errorlogs)
.bench.mtestargs = $(MTESTARGS) $(.mtestargs) --benchmark --verbose''')

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
        if s.endswith('-slow'):
            s = s[:-5]
            suites[s].speeds.append('slow')
        if s.endswith('-thorough'):
            s = s[:-9]
            suites[s].speeds.append('thorough')
        suites[s].deps.update(deps)

def emit_prolog(suites, prefix):
    all_targets = ' '.join((f'{prefix}-{k}' for k in suites.keys()))
    all_xml = ' '.join((f'{prefix}-report-{k}.junit.xml' for k in suites.keys()))
    print()
    print(f'all-{prefix}-targets = {all_targets}')
    print(f'all-{prefix}-xml = {all_xml}')
    print(f'.PHONY: {prefix} do-meson-{prefix} {prefix}-report.junit.xml $(all-{prefix}-targets) $(all-{prefix}-xml)')
    print(f'ifeq ($(filter {prefix}, $(MAKECMDGOALS)),)')
    print(f'.{prefix}.mtestargs += $(call .speed.$(SPEED), $(.{prefix}.mtest-suites))')
    print(f'endif')
    print(f'{prefix}-build: run-ninja')
    print(f'{prefix} $(all-{prefix}-targets): do-meson-{prefix}')
    print(f'do-meson-{prefix}: run-ninja; $(if $(MAKE.n),,+)$(MESON) test $(.{prefix}.mtestargs)')
    print(f'{prefix}-report.junit.xml $(all-{prefix}-xml): {prefix}-report%.junit.xml: run-ninja')
    print(f'\t$(MAKE) {prefix}$* MTESTARGS="$(MTESTARGS) --logbase {prefix}-report$*" && ln -f meson-logs/$@ .')

def emit_suite_deps(name, suite, prefix):
    deps = ' '.join(suite.deps)
    targets = [f'{prefix}-{name}', f'{prefix}-report-{name}.junit.xml', f'{prefix}', f'{prefix}-report.junit.xml',
               f'{prefix}-build']
    print()
    print(f'.{prefix}-{name}.deps = {deps}')
    for t in targets:
        print(f'.ninja-goals.{t} += $(.{prefix}-{name}.deps)')

def emit_suite(name, suite, prefix):
    emit_suite_deps(name, suite, prefix)
    targets = f'{prefix}-{name} {prefix}-report-{name}.junit.xml {prefix} {prefix}-report.junit.xml'
    print(f'ifneq ($(filter {targets}, $(MAKECMDGOALS)),)')
    print(f'.{prefix}.mtest-suites += ' + ' '.join(suite.names(name)))
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
