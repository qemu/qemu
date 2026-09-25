if 'qemu-mips64el' in emulators
  tcg_tests += {
    'mips64el-linux-user': {
      'cc': 'mips64el-linux-gnuabi64-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'cc_feat_cflags': cc_feat_cflags,
      'folder': 'mips64el',
      'gdb_arch': 'mips64el',
      'qemu': emulators['qemu-mips64el'],
      'tests': tcg_tests_mips64,
    }
  }
endif
