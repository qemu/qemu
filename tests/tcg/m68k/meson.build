tests = []

tests += tcg_tests['multiarch-linux-user']['tests']

tests += {
  'denormal.c': {},
  'trap.c': {},
}

if 'qemu-m68k' in emulators
  tcg_tests += {
    'm68k-linux-user': {
      'cc': 'm68k-linux-gnu-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'cc_docker_host_arch': ['x86_64'],
      'cc_feat_cflags': cc_feat_cflags,
      'folder': 'm68k',
      'gdb_arch': 'm68k',
      'qemu': emulators['qemu-m68k'],
      'tests': tests,
    }
  }
endif
