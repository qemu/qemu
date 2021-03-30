xtensa_ss = ss.source_set()

xtensa_cores = run_command('sh', '-c', 'cd $MESON_SOURCE_ROOT/$MESON_SUBDIR ; ls -1 core-*.c')
xtensa_ss.add(files(xtensa_cores.stdout().strip().split('\n')))

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

xtensa_softmmu_ss = ss.source_set()
xtensa_softmmu_ss.add(files(
  'dbg_helper.c',
  'mmu_helper.c',
  'monitor.c',
  'xtensa-semi.c',
))

target_arch += {'xtensa': xtensa_ss}
target_softmmu_arch += {'xtensa': xtensa_softmmu_ss}
