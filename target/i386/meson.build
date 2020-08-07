i386_ss = ss.source_set()
i386_ss.add(files(
  'cpu.c',
  'gdbstub.c',
  'helper.c',
  'xsave_helper.c',
))
i386_ss.add(when: 'CONFIG_TCG', if_true: files(
  'bpt_helper.c',
  'cc_helper.c',
  'excp_helper.c',
  'fpu_helper.c',
  'int_helper.c',
  'mem_helper.c',
  'misc_helper.c',
  'mpx_helper.c',
  'seg_helper.c',
  'smm_helper.c',
  'svm_helper.c',
  'translate.c'), if_false: files('tcg-stub.c'))
i386_ss.add(when: 'CONFIG_KVM', if_false: files('kvm-stub.c'))
i386_ss.add(when: 'CONFIG_SEV', if_true: files('sev.c'), if_false: files('sev-stub.c'))

i386_softmmu_ss = ss.source_set()
i386_softmmu_ss.add(files(
  'arch_dump.c',
  'arch_memory_mapping.c',
  'machine.c',
  'monitor.c',
))
i386_softmmu_ss.add(when: 'CONFIG_HYPERV', if_true: files('hyperv.c'), if_false: files('hyperv-stub.c'))
i386_softmmu_ss.add(when: 'CONFIG_KVM', if_true: files('kvm.c'))
i386_softmmu_ss.add(when: 'CONFIG_WHPX', if_true: files('whpx-all.c'))
i386_softmmu_ss.add(when: ['CONFIG_POSIX', 'CONFIG_HAX'], if_true: files('hax-all.c', 'hax-mem.c', 'hax-posix.c'))
i386_softmmu_ss.add(when: ['CONFIG_WIN32', 'CONFIG_HAX'], if_true: files('hax-all.c', 'hax-mem.c', 'hax-windows.c'))

subdir('hvf')

target_arch += {'i386': i386_ss}
target_softmmu_arch += {'i386': i386_softmmu_ss}
