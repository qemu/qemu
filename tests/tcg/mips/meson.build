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

tests_mips_msa = [
  '../mips/user/ase/msa/shift/test_msa_sra_d.c',
  '../mips/user/ase/msa/shift/test_msa_srl_b.c',
  '../mips/user/ase/msa/shift/test_msa_srl_d.c',
  '../mips/user/ase/msa/shift/test_msa_sll_b.c',
  '../mips/user/ase/msa/shift/test_msa_sra_w.c',
  '../mips/user/ase/msa/shift/test_msa_sra_h.c',
  '../mips/user/ase/msa/shift/test_msa_srar_h.c',
  '../mips/user/ase/msa/shift/test_msa_srlr_h.c',
  '../mips/user/ase/msa/shift/test_msa_srl_h.c',
  '../mips/user/ase/msa/shift/test_msa_sll_d.c',
  '../mips/user/ase/msa/shift/test_msa_srl_w.c',
  '../mips/user/ase/msa/shift/test_msa_srar_b.c',
  '../mips/user/ase/msa/shift/test_msa_sll_w.c',
  '../mips/user/ase/msa/shift/test_msa_sra_b.c',
  '../mips/user/ase/msa/shift/test_msa_sll_h.c',
  '../mips/user/ase/msa/shift/test_msa_srar_d.c',
  '../mips/user/ase/msa/shift/test_msa_srlr_d.c',
  '../mips/user/ase/msa/shift/test_msa_srar_w.c',
  '../mips/user/ase/msa/shift/test_msa_srlr_b.c',
  '../mips/user/ase/msa/shift/test_msa_srlr_w.c',
]
