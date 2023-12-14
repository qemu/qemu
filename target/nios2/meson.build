nios2_ss = ss.source_set()
nios2_ss.add(files(
  'cpu.c',
  'op_helper.c',
  'translate.c',
))

nios2_system_ss = ss.source_set()
nios2_system_ss.add(files(
  'helper.c',
  'monitor.c',
  'mmu.c',
  'nios2-semi.c',
))

target_arch += {'nios2': nios2_ss}
target_system_arch += {'nios2': nios2_system_ss}
