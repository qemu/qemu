mips_user_ss = ss.source_set()
mips_softmmu_ss = ss.source_set()
mips_ss = ss.source_set()
mips_ss.add(files(
  'cpu.c',
  'fpu.c',
  'gdbstub.c',
  'msa.c',
))

if have_system
  subdir('sysemu')
endif

if 'CONFIG_TCG' in config_all
  subdir('tcg')
endif

mips_ss.add(when: 'CONFIG_KVM', if_true: files('kvm.c'))

target_arch += {'mips': mips_ss}
target_softmmu_arch += {'mips': mips_softmmu_ss}
target_user_arch += {'mips': mips_user_ss}
