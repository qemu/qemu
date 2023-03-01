gen = decodetree.process('insns.decode')

hppa_ss = ss.source_set()
hppa_ss.add(gen)
hppa_ss.add(files(
  'cpu.c',
  'fpu_helper.c',
  'gdbstub.c',
  'helper.c',
  'int_helper.c',
  'op_helper.c',
  'translate.c',
))

hppa_softmmu_ss = ss.source_set()
hppa_softmmu_ss.add(files(
  'machine.c',
  'mem_helper.c',
  'sys_helper.c',
))

target_arch += {'hppa': hppa_ss}
target_softmmu_arch += {'hppa': hppa_softmmu_ss}
