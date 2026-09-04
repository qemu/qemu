cc = 'riscv64-linux-gnu-gcc'
cc_dockerfile = 'debian-all-test-cross'
cc_docker_host_arch = ['aarch64', 'x86_64']

subdir('system')

tests = []

# Multi arch tests
tests += tcg_tests['multiarch-linux-user']['tests']

tests += {
  'noexec.c': {},
  'test-aes.c': {'qemu_args': ['-cpu', 'rv64,zk=on']},
  'test-div.c': {},
  'test-fcvtmod.c': {
    'cflags': ['-march=rv64imafdc'],
    'qemu_args': ['-cpu', 'rv64,d=true,zfa=true'],
  },
  'test-noc.S': {
    'cflags': ['-nostdlib'],
    'qemu_args': ['-cpu', 'rv64,c=false'],
  },
}

if 'qemu-riscv64' in emulators
  tcg_tests += {
    'riscv64-linux-user': {
      'cc': cc,
      'cc_dockerfile': cc_dockerfile,
      'cc_docker_host_arch': cc_docker_host_arch,
      'folder': 'riscv64',
      'gdb_arch': 'riscv64',
      'qemu': emulators['qemu-riscv64'],
      'tests': tests,
    }
  }
endif
