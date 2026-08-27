tests = []

tests += tcg_tests['multiarch-linux-user']['tests']

tests += {
  'test-addv.c': {},
  'test-macl.c': {},
  'test-macw.c': {},
  'test-subv.c': {},
}

if 'qemu-sh4' in emulators
  tcg_tests += {
    'sh4-linux-user': {
      'cc': 'sh4-linux-gnu-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'cc_docker_host_arch': ['aarch64', 'x86_64'],
      'cc_feat_cflags': cc_feat_cflags,
      'folder': 'sh4',
      'gdb_arch': 'sh4',
      'qemu': emulators['qemu-sh4'],
      'tests': tests,
    }
  }
endif
