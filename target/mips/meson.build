mips_user_ss = ss.source_set()
mips_system_ss = ss.source_set()
mips_ss = ss.source_set()
mips_ss.add(files(
  'cpu.c',
  'fpu.c',
  'gdbstub.c',
  'msa.c',
))
mips_ss.add(zlib)

if have_system
  subdir('system')
endif

if 'CONFIG_TCG' in config_all_accel
  subdir('tcg')
endif

mips_ss.add(when: 'CONFIG_KVM', if_true: files('kvm.c'))

target_arch += {'mips': mips_ss}
target_system_arch += {'mips': mips_system_ss}
target_user_arch += {'mips': mips_user_ss}
