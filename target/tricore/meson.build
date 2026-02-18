tricore_ss = ss.source_set()
tricore_ss.add(files(
  'cpu.c',
  'fpu_helper.c',
  'helper.c',
  'op_helper.c',
  'translate.c',
))
tricore_ss.add(zlib)

tricore_user_ss = ss.source_set()
tricore_user_ss.add(files('gdbstub.c'))

tricore_common_system_ss = ss.source_set()
tricore_common_system_ss.add(files('gdbstub.c'))

target_arch += {'tricore': tricore_ss}
target_user_arch += {'tricore': tricore_user_ss}
target_common_system_arch += {'tricore': tricore_common_system_ss}
