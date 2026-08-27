cc = 'alpha-linux-gnu-gcc'
cc_dockerfile = 'debian-all-test-cross'
cc_docker_host_arch = ['aarch64', 'x86_64']

tests = []

# Multi arch tests
multi_src = []
foreach t: tcg_tests['multiarch-linux-user']['tests']
  foreach src, setup: t
    new_cflags = []
    if 'cflags' in setup
      new_cflags = setup['cflags']
    endif
    # Force generation of byte read/write
    if fs.stem(src) == 'test-plugin-mem-access'
      new_cflags += ['-mbwx']
    endif
    if src not in multi_src
      setup += {'cflags': new_cflags}
      multi_src += src
    endif
    tests += {src: setup}
  endforeach
endforeach

tests += {
  'hello-alpha.c': {},
  'test-cond.c': {},
  'test-cvttq.c': {},
  'test-ovf.c': {},
}

tests += {
  'test-cond.c': {
    'cflags': ['-DTEST_CMOV'],
    'exe_name': 'test-cmov'
  },
}

if 'qemu-alpha' in emulators
  tcg_tests += {
    'alpha-linux-user': {
      'cc': cc,
      'cc_dockerfile': cc_dockerfile,
      'cc_docker_host_arch': cc_docker_host_arch,
      'folder': 'alpha',
      'gdb_arch': 'alpha',
      'qemu': emulators['qemu-alpha'],
      'tests': tests,
    }
  }
endif
