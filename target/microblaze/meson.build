gen = decodetree.process('insns.decode')

microblaze_ss = ss.source_set()
microblaze_ss.add(gen)
microblaze_ss.add(files(
  'cpu.c',
  'gdbstub.c',
  'helper.c',
  'op_helper.c',
  'translate.c',
))

microblaze_softmmu_ss = ss.source_set()
microblaze_softmmu_ss.add(files(
  'mmu.c',
  'machine.c',
))

target_arch += {'microblaze': microblaze_ss}
target_softmmu_arch += {'microblaze': microblaze_softmmu_ss}
