sparc_ss = ss.source_set()
sparc_ss.add(files(
  'cc_helper.c',
  'cpu.c',
  'fop_helper.c',
  'gdbstub.c',
  'helper.c',
  'ldst_helper.c',
  'translate.c',
  'win_helper.c',
))
sparc_ss.add(when: 'TARGET_SPARC', if_true: files('int32_helper.c'))
sparc_ss.add(when: 'TARGET_SPARC64', if_true: files('int64_helper.c', 'vis_helper.c'))

sparc_softmmu_ss = ss.source_set()
sparc_softmmu_ss.add(files(
  'machine.c',
  'mmu_helper.c',
  'monitor.c',
))

target_arch += {'sparc': sparc_ss}
target_softmmu_arch += {'sparc': sparc_softmmu_ss}
