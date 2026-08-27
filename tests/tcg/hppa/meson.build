tests = []

tests += tcg_tests['multiarch-linux-user']['tests']

tests += {
  'stby.c': {'cflags': ['-pthread']},
}

if 'qemu-hppa' in emulators
  tcg_tests += {
    'hppa-linux-user': {
      'cc': 'hppa-linux-gnu-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'cc_docker_host_arch': ['x86_64'],
      'cc_feat_cflags': cc_feat_cflags,
      'folder': 'hppa',
      'gdb_arch': 'hppa',
      'qemu': emulators['qemu-hppa'],
      'tests': tests,
    }
  }
endif
