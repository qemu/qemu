mshv_ss = ss.source_set()
mshv_ss.add(if_true: files(
  'irq.c',
  'mem.c',
  'msr.c',
  'mshv-all.c'
))

specific_ss.add_all(when: 'CONFIG_MSHV', if_true: mshv_ss)
