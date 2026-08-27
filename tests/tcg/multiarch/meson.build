subdir('system')

tests = []

float_helpers = files('libs/float_helpers.c')[0]
float_cflags = [float_helpers, '-lm']

# GCC versions 12/13/14/15 at least incorrectly complain about
# "'SHA1Transform' reading 64 bytes from a region of size 0"; see the gcc bug
# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=106709
# Since this is just a standard piece of library code we've borrowed for a
# TCG test case, suppress the warning rather than trying to modify the
# code to work around the compiler.
sha1_cflags = ['-Wno-stringop-overread', '-Wno-unknown-warning-option']

# multiarch src path are written to be included from any other arch folder,
# so all arch can directly add those tests without any modification.
multiarch = meson.current_source_dir()

tests += {
  multiarch/'catch-syscalls.c': {},
  multiarch/'float_convd.c': {
    'cflags': float_cflags,
    'expected_output': 'float_convd.ref',
  },
  multiarch/'float_convs.c': {
    'cflags': float_cflags,
    'expected_output': 'float_convs.ref',
  },
  multiarch/'float_madds.c': {
    'cflags': float_cflags,
    'expected_output': 'float_madds.ref',
  },
  multiarch/'fnmsub.c': {'cflags': ['-lm']},
  multiarch/'follow-fork-mode.c': {},
  multiarch/'late-attach.c': {},
  multiarch/'munmap-pthread.c': {'cflags': ['-lpthread']},
  multiarch/'overflow.c': {},
  multiarch/'prot-none.c': {},
  multiarch/'segfault.c': {},
  multiarch/'sha1.c': {
    'cflags': sha1_cflags,
    'expected_output': multiarch/'sha1.ref'
  },
  multiarch/'sha512.c': {},
  multiarch/'sigbus.c': {},
  multiarch/'signals.c': {'cflags': ['-lrt', '-lpthread']},
  multiarch/'sigreturn-sigmask.c': {'cflags': ['-lpthread']},
  multiarch/'tb-link.c': {'cflags': ['-lpthread']},
  multiarch/'test-mmap.c': {},
  multiarch/'testthread.c': {'cflags': ['-lpthread']},
  multiarch/'threadcount.c': {'cflags': ['-lpthread']},
}

# Linux tests
tests += {
  multiarch/'linux/linux-madvise.c': {},
  multiarch/'linux/linux-shmat-maps.c': {},
  multiarch/'linux/linux-shmat-null.c': {},
  multiarch/'linux/linux-sigrtminmax.c': {},
  multiarch/'linux/linux-test.c': {},
  multiarch/'linux/test-vma.c': {},
}

# GDB tests
tests += {
  multiarch/'sha1.c': {
    'gdb_test': ['--test', files('gdbstub/sha1.py')],
  },
}
tests += {
  multiarch/'sha1.c': {
    'test_name': 'qxfer-auxv-read',
    'gdb_test': ['--test', files('gdbstub/test-qxfer-auxv-read.py')],
  },
}
tests += {
  multiarch/'sha1.c': {
    'test_name': 'proc-mappings',
    'gdb_test': ['--test', files('gdbstub/test-proc-mappings.py')],
  },
}
tests += {
  multiarch/'segfault.c': {
    'test_name': 'qxfer-siginfo-read',
    'gdb_test': ['--test', files('gdbstub/test-qxfer-siginfo-read.py')],
  },
  multiarch/'testthread.c': {
    'test_name': 'thread-breakpoint',
    'gdb_test': ['--test', files('gdbstub/test-thread-breakpoint.py')],
  },
  multiarch/'sha512.c': {
    'test_name': 'registers',
    'gdb_test': ['--test', files('gdbstub/registers.py')],
  },
  multiarch/'prot-none.c': {
    'gdb_test': ['--test', files('gdbstub/prot-none.py')],
    'env_var': 'PROT_NONE_PY=1',
  },
  multiarch/'catch-syscalls.c': {
    'gdb_test': ['--test', files('gdbstub/catch-syscalls.py')],
  },
  multiarch/'late-attach.c': {
    'gdb_test': ['--no-suspend', '--test', files('gdbstub/late-attach.py')],
    'env_var': 'LATE_ATTACH_PY=1',
  },
}

tests += {
  multiarch/'follow-fork-mode.c': {
    'test_name': 'follow-fork-mode-child',
    'gdb_test': ['--test', files('gdbstub/follow-fork-mode-child.py')],
  },
}
tests += {
  multiarch/'follow-fork-mode.c': {
    'test_name': 'follow-fork-mode-parent',
    'gdb_test': ['--test', files('gdbstub/follow-fork-mode-parent.py')],
  },
}

# Specific plugin tests
# Test plugin memory access instrumentation
tests += {
  multiarch/'plugin/test-plugin-mem-access.c': {
    'plugin_test': {
      'plugin': 'mem',
      'args': ['print-accesses=true']
    },
    'wrapper': [prog_check_plugin_output,
                find_program('plugin/regex-compare.sh')],
  },
  multiarch/'plugin/test-plugin-set-pc.c': {
    'plugin_test': {
      'plugin': 'setpc',
    }
  },
  multiarch/'test-plugin-syscall-filter.c': {
    'plugin_test': {
      'plugin': 'syscall',
    }
  },
}

tcg_tests += {
  'multiarch-linux-user': {
    'tests': tests
  }
}
