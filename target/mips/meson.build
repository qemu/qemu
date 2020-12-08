gen = [
  decodetree.process('mips32r6.decode', extra_args: '--static-decode=decode_mips32r6'),
  decodetree.process('mips64r6.decode', extra_args: '--static-decode=decode_mips64r6'),
  decodetree.process('msa32.decode', extra_args: '--static-decode=decode_msa32'),
  decodetree.process('msa64.decode', extra_args: '--static-decode=decode_msa64'),
]

mips_ss = ss.source_set()
mips_ss.add(gen)
mips_ss.add(files(
  'cpu.c',
  'gdbstub.c',
))
mips_ss.add(when: 'CONFIG_TCG', if_true: files(
  'dsp_helper.c',
  'fpu_helper.c',
  'lmmi_helper.c',
  'msa_helper.c',
  'msa_translate.c',
  'op_helper.c',
  'rel6_translate.c',
  'tlb_helper.c',
  'translate.c',
  'translate_addr_const.c',
))
mips_ss.add(when: 'CONFIG_KVM', if_true: files('kvm.c'))

mips_softmmu_ss = ss.source_set()
mips_softmmu_ss.add(files(
  'addr.c',
  'cp0_timer.c',
  'machine.c',
  'mips-semi.c',
))
mips_softmmu_ss.add(when: 'CONFIG_TCG', if_true: files(
  'cp0_helper.c',
))

target_arch += {'mips': mips_ss}
target_softmmu_arch += {'mips': mips_softmmu_ss}
