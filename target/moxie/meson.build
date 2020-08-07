moxie_ss = ss.source_set()
moxie_ss.add(files(
  'cpu.c',
  'helper.c',
  'machine.c',
  'machine.c',
  'translate.c',
))

moxie_softmmu_ss = ss.source_set()
moxie_softmmu_ss.add(files('mmu.c'))

target_arch += {'moxie': moxie_ss}
target_softmmu_arch += {'moxie': moxie_softmmu_ss}
