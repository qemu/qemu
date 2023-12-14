sh4_ss = ss.source_set()
sh4_ss.add(files(
  'cpu.c',
  'gdbstub.c',
  'helper.c',
  'op_helper.c',
  'translate.c',
))

sh4_system_ss = ss.source_set()
sh4_system_ss.add(files('monitor.c'))

target_arch += {'sh4': sh4_ss}
target_system_arch += {'sh4': sh4_system_ss}
