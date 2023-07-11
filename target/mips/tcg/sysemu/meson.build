mips_system_ss.add(files(
  'cp0_helper.c',
  'mips-semi.c',
  'special_helper.c',
  'tlb_helper.c',
))

mips_system_ss.add(when: 'TARGET_MIPS64', if_true: files(
  'lcsr_helper.c',
))
