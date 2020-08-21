alpha_ss = ss.source_set()
alpha_ss.add(files(
  'cpu.c',
  'fpu_helper.c',
  'gdbstub.c',
  'helper.c',
  'int_helper.c',
  'mem_helper.c',
  'sys_helper.c',
  'translate.c',
  'vax_helper.c',
))

alpha_softmmu_ss = ss.source_set()
alpha_softmmu_ss.add(files('machine.c'))

target_arch += {'alpha': alpha_ss}
target_softmmu_arch += {'alpha': alpha_softmmu_ss}
