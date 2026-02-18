alpha_ss = ss.source_set()
alpha_ss.add(files(
  'cpu.c',
  'fpu_helper.c',
  'helper.c',
  'clk_helper.c',
  'int_helper.c',
  'mem_helper.c',
  'translate.c',
  'vax_helper.c',
))

alpha_user_ss = ss.source_set()
alpha_user_ss.add(files(
  'gdbstub.c',
))

alpha_common_system_ss = ss.source_set()
alpha_common_system_ss.add(files(
  'gdbstub.c',
  'machine.c',
  'sys_helper.c',
))

target_arch += {'alpha': alpha_ss}
target_user_arch += {'alpha': alpha_user_ss}
target_common_system_arch += {'alpha': alpha_common_system_ss}
