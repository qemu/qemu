m68k_ss = ss.source_set()
m68k_ss.add(files(
  'cpu.c',
  'fpu_helper.c',
  'gdbstub.c',
  'helper.c',
  'm68k-semi.c',
  'op_helper.c',
  'softfloat.c',
  'translate.c',
))

m68k_softmmu_ss = ss.source_set()
m68k_softmmu_ss.add(files('monitor.c'))

target_arch += {'m68k': m68k_ss}
target_softmmu_arch += {'m68k': m68k_softmmu_ss}
