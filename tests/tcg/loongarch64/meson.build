cc = 'loongarch64-unknown-linux-gnu-gcc'
cc_dockerfile = 'debian-loongarch-cross'
cc_docker_host_arch = ['x86_64']

tests = []

tests += tcg_tests['multiarch-linux-user']['tests']

tests += {
  'test_bit.c': {},
  'test_fclass.c': {},
  'test_fcsr.c': {},
  'test_fpcom.c': {},
  'test_div.c': {},
  'test_pcadd.c': {},
}

if 'qemu-loongarch64' in emulators
  tcg_tests += {
    'loongarch64-linux-user': {
      'cc': cc,
      'cc_dockerfile': cc_dockerfile,
      'cc_docker_host_arch': cc_docker_host_arch,
      'folder': 'loongarch64',
      'gdb_arch': 'loongarch64',
      'qemu': emulators['qemu-loongarch64'],
      'tests': tests,
    }
  }
endif
