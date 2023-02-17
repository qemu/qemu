gen = [
  decodetree.process('sve.decode', extra_args: '--decode=disas_sve'),
  decodetree.process('sme.decode', extra_args: '--decode=disas_sme'),
  decodetree.process('sme-fa64.decode', extra_args: '--static-decode=disas_sme_fa64'),
  decodetree.process('neon-shared.decode', extra_args: '--decode=disas_neon_shared'),
  decodetree.process('neon-dp.decode', extra_args: '--decode=disas_neon_dp'),
  decodetree.process('neon-ls.decode', extra_args: '--decode=disas_neon_ls'),
  decodetree.process('vfp.decode', extra_args: '--decode=disas_vfp'),
  decodetree.process('vfp-uncond.decode', extra_args: '--decode=disas_vfp_uncond'),
  decodetree.process('m-nocp.decode', extra_args: '--decode=disas_m_nocp'),
  decodetree.process('mve.decode', extra_args: '--decode=disas_mve'),
  decodetree.process('a32.decode', extra_args: '--static-decode=disas_a32'),
  decodetree.process('a32-uncond.decode', extra_args: '--static-decode=disas_a32_uncond'),
  decodetree.process('t32.decode', extra_args: '--static-decode=disas_t32'),
  decodetree.process('t16.decode', extra_args: ['-w', '16', '--static-decode=disas_t16']),
]

arm_ss.add(gen)

arm_ss.add(files(
  'translate.c',
  'translate-m-nocp.c',
  'translate-mve.c',
  'translate-neon.c',
  'translate-vfp.c',
  'crypto_helper.c',
  'hflags.c',
  'iwmmxt_helper.c',
  'm_helper.c',
  'mve_helper.c',
  'neon_helper.c',
  'op_helper.c',
  'tlb_helper.c',
  'vec_helper.c',
))

arm_ss.add(when: 'TARGET_AARCH64', if_true: files(
  'translate-a64.c',
  'translate-sve.c',
  'translate-sme.c',
  'helper-a64.c',
  'mte_helper.c',
  'pauth_helper.c',
  'sme_helper.c',
  'sve_helper.c',
))

arm_softmmu_ss.add(files(
  'psci.c',
))
