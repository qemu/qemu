xtensa_ss = ss.source_set()
xtensa_ss.add(files(
  'core-dc232b.c',
  'core-dc233c.c',
  'core-de212.c',
  'core-de233_fpu.c',
  'core-dsp3400.c',
  'core-fsf.c',
  'core-sample_controller.c',
  'core-test_kc705_be.c',
  'core-test_mmuhifi_c3.c',
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
