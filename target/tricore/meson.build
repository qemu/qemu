tricore_ss = ss.source_set()
tricore_ss.add(files(
  'cpu.c',
  'fpu_helper.c',
  'helper.c',
  'op_helper.c',
  'translate.c',
  'gdbstub.c',
))
tricore_ss.add(zlib)

tricore_system_ss = ss.source_set()

target_arch += {'tricore': tricore_ss}
target_system_arch += {'tricore': tricore_system_ss}
