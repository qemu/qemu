if 'CONFIG_TCG' not in config_all_accel
  subdir_done()
endif

riscv_ss.add(files(
  'bitmanip_helper.c',
 'cpu_helper.c',
  'csr.c',
  'crypto_helper.c',
  'fpu_helper.c',
  'm128_helper.c',
  'op_helper.c',
  'translate.c',
  'tcg-cpu.c',
  'vcrypto_helper.c',
  'vector_helper.c',
  'vector_internals.c',
  'xsf_helper.c',
  'zce_helper.c'))


riscv_system_ss.add(files(
  'debug.c',
  'mips_csr.c',
  'pmp.c',
  'pmu.c',
  'th_csr.c',
))


