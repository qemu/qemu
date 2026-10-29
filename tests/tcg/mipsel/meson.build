tests = []

# Multi arch tests
tests += tcg_tests['multiarch-linux-user']['tests']

tests_dsp = [
  'test_dsp_acc.c',
  'test_dsp_arith.c',
  'test_dsp_cmp.c',
  'test_dsp_r1_bitrev.c',
  'test_dsp_r1_bposge32.c',
  'test_dsp_r1_dpaq_s_w_ph.c',
  'test_dsp_r1_dpaq_sa_l_w.c',
  'test_dsp_r1_dpau_h_qbl.c',
  'test_dsp_r1_dpau_h_qbr.c',
  'test_dsp_r1_dpsq_s_w_ph.c',
  'test_dsp_r1_dpsq_sa_l_w.c',
  'test_dsp_r1_dpsu_h_qbl.c',
  'test_dsp_r1_dpsu_h_qbr.c',
  'test_dsp_r1_insv.c',
  'test_dsp_r1_lbux.c',
  'test_dsp_r1_lhx.c',
  'test_dsp_r1_lwx.c',
  'test_dsp_r1_madd.c',
  'test_dsp_r1_maddu.c',
  'test_dsp_r1_maq_s_w_phl.c',
  'test_dsp_r1_maq_s_w_phr.c',
  'test_dsp_r1_maq_sa_w_phl.c',
  'test_dsp_r1_maq_sa_w_phr.c',
  'test_dsp_r1_msub.c',
  'test_dsp_r1_msubu.c',
  'test_dsp_r1_muleq_s_w_phl.c',
  'test_dsp_r1_muleq_s_w_phr.c',
  'test_dsp_r1_muleu_s_ph_qbl.c',
  'test_dsp_r1_muleu_s_ph_qbr.c',
  'test_dsp_r1_mulq_rs_ph.c',
  'test_dsp_r1_mult.c',
  'test_dsp_r1_multu.c',
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
  'test_dsp_r1_rddsp.c',
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
  'test_dsp_r1_wrdsp.c',
  'test_dsp_r2_append.c',
  'test_dsp_r2_balign.c',
  'test_dsp_r2_dpa_w_ph.c',
  'test_dsp_r2_dpaqx_s_w_ph.c',
  'test_dsp_r2_dpaqx_sa_w_ph.c',
  'test_dsp_r2_dpax_w_ph.c',
  'test_dsp_r2_dps_w_ph.c',
  'test_dsp_r2_dpsqx_s_w_ph.c',
  'test_dsp_r2_dpsqx_sa_w_ph.c',
  'test_dsp_r2_dpsx_w_ph.c',
  'test_dsp_r2_mul_ph.c',
  'test_dsp_r2_mul_s_ph.c',
  'test_dsp_r2_mulq_rs_w.c',
  'test_dsp_r2_mulq_s_ph.c',
  'test_dsp_r2_mulq_s_w.c',
  'test_dsp_r2_mulsa_w_ph.c',
  'test_dsp_r2_mulsaq_s_w_ph.c',
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
