cris_ss = ss.source_set()
cris_ss.add(files(
  'cpu.c',
  'gdbstub.c',
  'op_helper.c',
  'translate.c',
))

cris_system_ss = ss.source_set()
cris_system_ss.add(files(
  'helper.c',
  'machine.c',
  'mmu.c',
))

target_arch += {'cris': cris_ss}
target_system_arch += {'cris': cris_system_ss}
