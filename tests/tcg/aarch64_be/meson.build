tests = []

# As we don't have any big-endian libc available,
# the best we can do is a basic Hello World.
tests += {
  'hello.c': {'cflags': ['-mbig-endian', '-ffreestanding', '-nostdlib']},
}

if 'qemu-aarch64_be' in emulators
  tcg_tests += {
    'aarch64_be-linux-user': {
      'cc': 'aarch64-linux-gnu-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'cc_docker_host_arch': ['aarch64', 'x86_64'],
      'cc_feat_cflags': cc_feat_cflags,
      'folder': 'aarch64_be',
      'gdb_arch': 'aarch64_be',
      'qemu': emulators['qemu-aarch64_be'],
      'tests': tests,
    }
  }
endif
