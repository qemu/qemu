tests = []

tests += tcg_tests['multiarch-linux-user']['tests']

tests += {
  '../mips/user/isa/octeon/octeon-insns.c': {
    'qemu_args': ['-cpu', 'Octeon68XX'],
  },
}

# export tests for mips64el
tcg_tests_mips64 = tests

if 'qemu-mips64' in emulators
  tcg_tests += {
    'mips64-linux-user': {
      'cc': 'mips64-linux-gnuabi64-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'cc_docker_host_arch': ['aarch64', 'x86_64'],
      'cc_feat_cflags': cc_feat_cflags,
      'folder': 'mips64',
      'gdb_arch': 'mips64',
      'qemu': emulators['qemu-mips64'],
      'tests': tcg_tests_mips64,
    }
  }
endif
