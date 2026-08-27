if 'qemu-ppc64le' in emulators
  tcg_tests += {
    'ppc64le-linux-user': {
      'cc': 'powerpc64le-linux-gnu-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'cc_docker_host_arch': ['x86_64'],
      'cc_feat_cflags': cc_feat_cflags,
      'folder': 'ppc64le',
      'gdb_arch': 'ppc64le',
      'qemu': emulators['qemu-ppc64le'],
      'tests': tcg_tests_ppc64,
    }
  }
endif
