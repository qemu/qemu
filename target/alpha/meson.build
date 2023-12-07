alpha_ss = ss.source_set()
alpha_ss.add(files(
  'cpu.c',
  'fpu_helper.c',
  'gdbstub.c',
  'helper.c',
  'clk_helper.c',
  'int_helper.c',
  'mem_helper.c',
  'translate.c',
  'vax_helper.c',
))

alpha_system_ss = ss.source_set()
alpha_system_ss.add(files(
  'machine.c',
  'sys_helper.c',
))

target_arch += {'alpha': alpha_ss}
target_system_arch += {'alpha': alpha_system_ss}
