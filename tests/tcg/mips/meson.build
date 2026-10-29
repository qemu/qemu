tests = []

# MSA tests shared by the mipsel and mips64 targets
tests_mips_msa_groups = [
  '../mips/test_msa_bit_count.c',
  '../mips/test_msa_bit_move.c',
  '../mips/test_msa_bit_set.c',
  '../mips/test_msa_fixed_multiply.c',
  '../mips/test_msa_float_max_min.c',
  '../mips/test_msa_int_add.c',
  '../mips/test_msa_int_average.c',
  '../mips/test_msa_int_compare.c',
  '../mips/test_msa_int_div_mod.c',
  '../mips/test_msa_int_dot_product.c',
  '../mips/test_msa_int_max_min.c',
  '../mips/test_msa_int_multiply.c',
  '../mips/test_msa_int_subtract.c',
  '../mips/test_msa_interleave.c',
  '../mips/test_msa_logic.c',
  '../mips/test_msa_pack.c',
  '../mips/test_msa_shift.c',
]

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
      'folder': 'mips',
      'gdb_arch': 'mips',
      'qemu': emulators['qemu-mips'],
      'tests': tests,
    }
  }
endif
