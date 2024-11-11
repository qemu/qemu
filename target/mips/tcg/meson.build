gen = [
  decodetree.process('mips16e_16.decode', extra_args: ['--decode=decode_ase_mips16e_16', '--insnwidth=16']),
  decodetree.process('mips16e_32.decode', extra_args: ['--decode=decode_ase_mips16e_32']),
  decodetree.process('micromips16.decode', extra_args: ['--decode=decode_isa_micromips16', '--insnwidth=16']),
  decodetree.process('micromips32.decode', extra_args: ['--decode=decode_isa_micromips32']),
  decodetree.process('micromips32_r6.decode', extra_args: ['--decode=decode_isa_micromips32_r6']),
  decodetree.process('rel6.decode', extra_args: ['--decode=decode_isa_rel6']),
  decodetree.process('nanomips16.decode', extra_args: ['--decode=decode_isa_nanomips16', '--insnwidth=16']),
  decodetree.process('nanomips32.decode', extra_args: ['--decode=decode_isa_nanomips32']),
  decodetree.process('nanomips48.decode', extra_args: ['--decode=decode_isa_nanomips48', '--insnwidth=48']),
  decodetree.process('msa.decode', extra_args: '--decode=decode_ase_msa'),
  decodetree.process('tx79.decode', extra_args: '--static-decode=decode_tx79'),
  decodetree.process('vr54xx.decode', extra_args: '--decode=decode_ext_vr54xx'),
  decodetree.process('octeon.decode', extra_args: '--decode=decode_ext_octeon'),
  decodetree.process('lcsr.decode', extra_args: '--decode=decode_ase_lcsr'),
  decodetree.process('godson2.decode', extra_args: ['--static-decode=decode_godson2']),
  decodetree.process('loong-ext.decode', extra_args: ['--static-decode=decode_loong_ext']),
]

mips_ss.add(gen)
mips_ss.add(files(
  'dsp_helper.c',
  'exception.c',
  'fpu_helper.c',
  'ldst_helper.c',
  'lmmi_helper.c',
  'micromips_translate.c',
  'mips16e_translate.c',
  'msa_helper.c',
  'msa_translate.c',
  'nanomips_translate.c',
  'octeon_crypto.c',
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
  'loong_translate.c',
), if_false: files(
  'mxu_translate.c',
))

if have_system
  subdir('system')
endif
