tests = []

# Multi arch tests
tests += tcg_tests['multiarch-linux-user']['tests']

tests_dsp = [
  'test_dsp_acc.c',
  'test_dsp_arith.c',
  'test_dsp_cmp.c',
  'test_dsp_ctrl.c',
  'test_dsp_dot.c',
  'test_dsp_mul.c',
  'test_dsp_r1_bitrev.c',
  'test_dsp_r1_insv.c',
  'test_dsp_r1_packrl_ph.c',
  'test_dsp_r1_preceq_w_phl.c',
  'test_dsp_r1_preceq_w_phr.c',
  'test_dsp_r1_precequ_ph_qbl.c',
  'test_dsp_r1_precequ_ph_qbla.c',
  'test_dsp_r1_precequ_ph_qbr.c',
  'test_dsp_r1_precequ_ph_qbra.c',
  'test_dsp_r1_preceu_ph_qbl.c',
  'test_dsp_r1_preceu_ph_qbla.c',
  'test_dsp_r1_preceu_ph_qbr.c',
  'test_dsp_r1_preceu_ph_qbra.c',
  'test_dsp_r1_precrq_ph_w.c',
  'test_dsp_r1_precrq_qb_ph.c',
  'test_dsp_r1_precrq_rs_ph_w.c',
  'test_dsp_r1_precrqu_s_qb_ph.c',
  'test_dsp_r1_repl_ph.c',
  'test_dsp_r1_repl_qb.c',
  'test_dsp_r1_replv_ph.c',
  'test_dsp_r1_replv_qb.c',
  'test_dsp_r1_shll_ph.c',
  'test_dsp_r1_shll_qb.c',
  'test_dsp_r1_shll_s_ph.c',
  'test_dsp_r1_shll_s_w.c',
  'test_dsp_r1_shllv_ph.c',
  'test_dsp_r1_shllv_qb.c',
  'test_dsp_r1_shllv_s_ph.c',
  'test_dsp_r1_shllv_s_w.c',
  'test_dsp_r1_shra_ph.c',
  'test_dsp_r1_shra_r_ph.c',
  'test_dsp_r1_shra_r_w.c',
  'test_dsp_r1_shrav_ph.c',
  'test_dsp_r1_shrav_r_ph.c',
  'test_dsp_r1_shrav_r_w.c',
  'test_dsp_r1_shrl_qb.c',
  'test_dsp_r1_shrlv_qb.c',
  'test_dsp_r2_append.c',
  'test_dsp_r2_balign.c',
  'test_dsp_r2_precr_qb_ph.c',
  'test_dsp_r2_precr_sra_ph_w.c',
  'test_dsp_r2_precr_sra_r_ph_w.c',
  'test_dsp_r2_prepend.c',
  'test_dsp_r2_shra_qb.c',
  'test_dsp_r2_shra_r_qb.c',
  'test_dsp_r2_shrav_qb.c',
  'test_dsp_r2_shrav_r_qb.c',
  'test_dsp_r2_shrl_ph.c',
  'test_dsp_r2_shrlv_ph.c',
]

foreach src: tests_dsp
  tests += {
    src: {
      'cflags': ['-EL', '-mabi=32', '-march=mips32r2', '-mgp32', '-mdsp', '-mdspr2',
                 '-Wno-unused-but-set-variable', '-Wno-unused-variable'],
      'qemu_args': ['-cpu', '74Kf'],
    }
  }
endforeach

if 'qemu-mipsel' in emulators
  tcg_tests += {
    'mipsel-linux-user': {
      'cc': 'mipsel-linux-gnu-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'folder': 'mipsel',
      'gdb_arch': 'mipsel',
      'qemu': emulators['qemu-mipsel'],
      'tests': tests,
    }
  }
endif

tests = []

foreach src: tests_mips_msa
  tests += {
    src: {
      'cflags': ['-mabi=32', '-march=mips32r5', '-mmsa', '-mno-odd-spreg',
                 '-mfp64', '-mnan=2008'],
      'qemu_args': ['-cpu', 'P5600'],
    }
  }
endforeach

if 'qemu-mipsel' in emulators
  tcg_tests += {
    'mips32r5el-linux-user': {
      'cc': 'mips32r5el-buildroot-linux-gnu-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'folder': 'mipsel',
      'gdb_arch': 'mipsel',
      'qemu': emulators['qemu-mipsel'],
      'tests': tests,
    }
  }
endif
