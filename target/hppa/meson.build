gen = decodetree.process('insns.decode')

hppa_ss = ss.source_set()
hppa_ss.add(gen)
hppa_ss.add(files(
  'cpu.c',
  'fpu_helper.c',
  'gdbstub.c',
  'helper.c',
  'op_helper.c',
  'translate.c',
))

hppa_system_ss = ss.source_set()
hppa_system_ss.add(files(
  'int_helper.c',
  'machine.c',
  'mem_helper.c',
  'sys_helper.c',
))

target_arch += {'hppa': hppa_ss}
target_system_arch += {'hppa': hppa_system_ss}
