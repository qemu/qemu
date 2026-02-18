sh4_ss = ss.source_set()
sh4_ss.add(files(
  'cpu.c',
  'helper.c',
  'op_helper.c',
  'translate.c',
))

sh4_common_system_ss = ss.source_set()
sh4_common_system_ss.add(files(
  'gdbstub.c',
  'monitor.c',
))

sh4_user_ss = ss.source_set()
sh4_user_ss.add(files('gdbstub.c'))

target_arch += {'sh4': sh4_ss}
target_user_arch += {'sh4': sh4_user_ss}
target_common_system_arch += {'sh4': sh4_common_system_ss}
