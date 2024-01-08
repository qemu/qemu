if 'CONFIG_TCG' not in config_all_accel
  subdir_done()
endif

loongarch_ss.add([zlib, gen])

loongarch_ss.add(files(
  'fpu_helper.c',
  'op_helper.c',
  'translate.c',
  'vec_helper.c',
))

loongarch_system_ss.add(files(
  'constant_timer.c',
  'csr_helper.c',
  'iocsr_helper.c',
  'tlb_helper.c',
))
