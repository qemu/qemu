gen = [
  decodetree.process('rel6.decode', extra_args: ['--decode=decode_isa_rel6']),
  decodetree.process('msa.decode', extra_args: '--decode=decode_ase_msa'),
  decodetree.process('tx79.decode', extra_args: '--static-decode=decode_tx79'),
  decodetree.process('vr54xx.decode', extra_args: '--decode=decode_ext_vr54xx'),
  decodetree.process('octeon.decode', extra_args: '--decode=decode_ext_octeon'),
  decodetree.process('lcsr.decode', extra_args: '--decode=decode_ase_lcsr'),
]

mips_ss.add(gen)
mips_ss.add(files(
  'dsp_helper.c',
  'exception.c',
  'fpu_helper.c',
  'ldst_helper.c',
  'lmmi_helper.c',
  'msa_helper.c',
  'msa_translate.c',
  'op_helper.c',
  'rel6_translate.c',
  'translate.c',
  'translate_addr_const.c',
  'txx9_translate.c',
  'vr54xx_helper.c',
  'vr54xx_translate.c',
))
mips_ss.add(when: 'TARGET_MIPS64', if_true: files(
  'tx79_translate.c',
  'octeon_translate.c',
  'lcsr_translate.c',
), if_false: files(
  'mxu_translate.c',
))

if have_system
  subdir('sysemu')
endif
