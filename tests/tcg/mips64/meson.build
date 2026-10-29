tests = []

tests += tcg_tests['multiarch-linux-user']['tests']

tests += {
  '../mips64/octeon-insns.c': {
    'qemu_args': ['-cpu', 'Octeon68XX'],
    'protocol': 'tap',
  },
}

# export tests for mips64el
tcg_tests_mips64 = tests

if 'qemu-mips64' in emulators
  tcg_tests += {
    'mips64-linux-user': {
      'cc': 'mips64-linux-gnuabi64-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'folder': 'mips64',
      'gdb_arch': 'mips64',
      'qemu': emulators['qemu-mips64'],
      'tests': tcg_tests_mips64,
    }
  }
endif

tests = []

tests_mips64r6 = [
  '../mips64/test_mips64r6_bitops.c',
  '../mips64/test_mips64r6_crc.c',
  '../mips64/test_mips64r6_logic.c',
  '../mips64/test_mips64r6_dmuh.c',
  '../mips64/test_mips64r6_dmuhu.c',
  '../mips64/test_mips64r6_dmul.c',
  '../mips64/test_mips64r6_dmulu.c',
  '../mips64/test_mips64r6_dsllv.c',
  '../mips64/test_mips64r6_dsrav.c',
  '../mips64/test_mips64r6_dsrlv.c',
  '../mips64/test_mips64r6_muh.c',
  '../mips64/test_mips64r6_muhu.c',
  '../mips64/test_mips64r6_mul.c',
  '../mips64/test_mips64r6_mulu.c',
  '../mips64/test_mips64r6_sllv.c',
  '../mips64/test_mips64r6_srav.c',
  '../mips64/test_mips64r6_srlv.c',
]

foreach src: tests_mips64r6
  tests += {src: {'cflags': '-march=mips64r6'}}
endforeach

foreach src: tests_mips_msa
  tests += {
    src: {
      'cflags': ['-march=mips64r6', '-mmsa'],
      'qemu_args': ['-cpu', 'I6400'],
    }
  }
endforeach

# export tests for mips64el
tcg_tests_mips64r6 = tests

if 'qemu-mips64' in emulators
  tcg_tests += {
    'mips64r6-linux-user': {
      'cc': 'mipsisa64r6-linux-gnuabi64-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'folder': 'mips64',
      'gdb_arch': 'mips64',
      'qemu': emulators['qemu-mips64'],
      'tests': tcg_tests_mips64r6,
    }
  }
endif
