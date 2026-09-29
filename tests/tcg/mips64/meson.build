tests = []

tests += tcg_tests['multiarch-linux-user']['tests']

tests += {
  '../mips64/octeon-insns.c': {
    'qemu_args': ['-cpu', 'Octeon68XX'],
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
  '../mips64/test_mips64r6_and.c',
  '../mips64/test_mips64r6_bitswap.c',
  '../mips64/test_mips64r6_clo.c',
  '../mips64/test_mips64r6_clz.c',
  '../mips64/test_mips64r6_crc32b.c',
  '../mips64/test_mips64r6_crc32cb.c',
  '../mips64/test_mips64r6_crc32cd.c',
  '../mips64/test_mips64r6_crc32ch.c',
  '../mips64/test_mips64r6_crc32cw.c',
  '../mips64/test_mips64r6_crc32d.c',
  '../mips64/test_mips64r6_crc32h.c',
  '../mips64/test_mips64r6_crc32w.c',
  '../mips64/test_mips64r6_dbitswap.c',
  '../mips64/test_mips64r6_dclo.c',
  '../mips64/test_mips64r6_dclz.c',
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
  '../mips64/test_mips64r6_nor.c',
  '../mips64/test_mips64r6_or.c',
  '../mips64/test_mips64r6_sllv.c',
  '../mips64/test_mips64r6_srav.c',
  '../mips64/test_mips64r6_srlv.c',
  '../mips64/test_mips64r6_xor.c',
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
