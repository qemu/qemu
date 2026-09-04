tests = []

tests += tcg_tests['multiarch-linux-user']['tests']

tests += {
  'hello-mips.c': {
    'cflags': ['-mno-abicalls', '-fno-PIC', '-fno-stack-protector', '-mabi=32',
               '-nostdlib']
  }
}

if 'qemu-mips' in emulators
  tcg_tests += {
    'mips-linux-user': {
      'cc': 'mips-linux-gnu-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'cc_docker_host_arch': ['aarch64', 'x86_64'],
      'cc_feat_cflags': cc_feat_cflags,
      'folder': 'mips',
      'gdb_arch': 'mips',
      'qemu': emulators['qemu-mips'],
      'tests': tests,
    }
  }
endif
