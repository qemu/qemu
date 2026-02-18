gen = decodetree.process('insns.decode')

microblaze_ss = ss.source_set()
microblaze_ss.add(gen)
microblaze_ss.add(files(
  'cpu.c',
  'helper.c',
  'op_helper.c',
  'translate.c',
))

microblaze_user_ss = ss.source_set()
microblaze_user_ss.add(files('gdbstub.c'))

microblaze_common_system_ss = ss.source_set()
microblaze_common_system_ss.add(files('gdbstub.c'))

microblaze_system_ss = ss.source_set()
microblaze_system_ss.add(files(
  'mmu.c',
  'machine.c',
))

target_arch += {'microblaze': microblaze_ss}
target_user_arch += {'microblaze': microblaze_user_ss}
target_system_arch += {'microblaze': microblaze_system_ss}
target_common_system_arch += {'microblaze': microblaze_common_system_ss}
