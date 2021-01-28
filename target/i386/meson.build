i386_ss = ss.source_set()
i386_ss.add(files(
  'cpu.c',
  'gdbstub.c',
  'helper.c',
  'xsave_helper.c',
  'cpu-dump.c',
))
i386_ss.add(when: 'CONFIG_SEV', if_true: files('sev.c'), if_false: files('sev-stub.c'))

i386_softmmu_ss = ss.source_set()
i386_softmmu_ss.add(files(
  'arch_dump.c',
  'arch_memory_mapping.c',
  'machine.c',
  'monitor.c',
))

subdir('kvm')
subdir('hax')
subdir('whpx')
subdir('hvf')
subdir('tcg')

target_arch += {'i386': i386_ss}
target_softmmu_arch += {'i386': i386_softmmu_ss}
