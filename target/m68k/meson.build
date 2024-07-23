m68k_ss = ss.source_set()
m68k_ss.add(files(
  'cpu.c',
  'fpu_helper.c',
  'gdbstub.c',
  'helper.c',
  'op_helper.c',
  'softfloat.c',
  'translate.c',
))

m68k_system_ss = ss.source_set()
m68k_system_ss.add(files(
  'monitor.c'
))
m68k_system_ss.add(when: ['CONFIG_SEMIHOSTING'],
  if_true: files('m68k-semi.c'),
  if_false: files('semihosting-stub.c')
)

target_arch += {'m68k': m68k_ss}
target_system_arch += {'m68k': m68k_system_ss}
