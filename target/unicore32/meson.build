unicore32_ss = ss.source_set()
unicore32_ss.add(files(
  'cpu.c',
  'helper.c',
  'op_helper.c',
  'translate.c',
  'ucf64_helper.c',
), curses)

unicore32_softmmu_ss = ss.source_set()
unicore32_softmmu_ss.add(files('softmmu.c'))

target_arch += {'unicore32': unicore32_ss}
target_softmmu_arch += {'unicore32': unicore32_softmmu_ss}
