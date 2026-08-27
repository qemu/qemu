tests = []

# multiarch src path are written to be included from any other arch folder,
# so all arch can directly add those tests.
multiarch = meson.current_source_dir()

tests += {
  multiarch/'hello.c': {},
  multiarch/'interrupt.c': {},
  multiarch/'memory.c': {},
}

tests += {
  multiarch/'memory.c': {
    'gdb_test': ['--test', files('../gdbstub/memory.py')]
  },
  multiarch/'interrupt.c': {
    'gdb_test': ['--test', files('../gdbstub/interrupt.py')]
  },
  multiarch/'hello.c': {
    'test_name': 'untimely-packet',
    'gdb_test': ['--gdb-args',  '-ex \'set debug remote 1\'',
                 '--output', '/dev/stdout',
                 '--stderr', '/dev/stdout'],
    'wrapper': [prog_run_and_check_forbidden_output,
                'Packet instead of Ack, ignoring it']
  }
}

tests += {
  multiarch/'memory.c': {
    'test_name': 'registers',
    'gdb_test': ['--test', files('../gdbstub/registers.py')]
  }
}

tests += {
  multiarch/'memory.c': {
    'test_name': 'memory-access',
    'plugin_test': {
      'plugin': 'mem',
      'args': ['region-summary=true'],
    },
    'wrapper': [prog_check_plugin_output,
                find_program('./validate-memory-counts.py')]
  }
}

tcg_tests += {
  'multiarch-softmmu': {
    'tests': tests
  }
}
