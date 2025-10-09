i386_mshv_ss = ss.source_set()

i386_mshv_ss.add(files(
  'mshv-cpu.c',
  'x86.c',
))

i386_system_ss.add_all(when: 'CONFIG_MSHV', if_true: i386_mshv_ss)
