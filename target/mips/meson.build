mips_ss = ss.source_set()
mips_ss.add(files(
  'cp0_helper.c',
  'cpu.c',
  'dsp_helper.c',
  'fpu_helper.c',
  'gdbstub.c',
  'helper.c',
  'lmmi_helper.c',
  'msa_helper.c',
  'op_helper.c',
  'translate.c',
))
mips_ss.add(when: 'CONFIG_KVM', if_true: files('kvm.c'))

mips_softmmu_ss = ss.source_set()
mips_softmmu_ss.add(files(
  'cp0_timer.c',
  'machine.c',
  'mips-semi.c',
))

target_arch += {'mips': mips_ss}
target_softmmu_arch += {'mips': mips_softmmu_ss}
