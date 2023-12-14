xtensa_ss = ss.source_set()

xtensa_cores = fs.read('cores.list')
xtensa_ss.add(files(xtensa_cores.strip().split('\n')))

xtensa_ss.add(files(
  'cpu.c',
  'exc_helper.c',
  'fpu_helper.c',
  'gdbstub.c',
  'helper.c',
  'op_helper.c',
  'translate.c',
  'win_helper.c',
  'xtensa-isa.c',
))

xtensa_system_ss = ss.source_set()
xtensa_system_ss.add(files(
  'dbg_helper.c',
  'mmu_helper.c',
  'monitor.c',
  'xtensa-semi.c',
))

target_arch += {'xtensa': xtensa_ss}
target_system_arch += {'xtensa': xtensa_system_ss}
