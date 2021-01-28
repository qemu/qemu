gen = [
  decodetree.process('sve.decode', extra_args: '--decode=disas_sve'),
  decodetree.process('neon-shared.decode', extra_args: '--static-decode=disas_neon_shared'),
  decodetree.process('neon-dp.decode', extra_args: '--static-decode=disas_neon_dp'),
  decodetree.process('neon-ls.decode', extra_args: '--static-decode=disas_neon_ls'),
  decodetree.process('vfp.decode', extra_args: '--static-decode=disas_vfp'),
  decodetree.process('vfp-uncond.decode', extra_args: '--static-decode=disas_vfp_uncond'),
  decodetree.process('m-nocp.decode', extra_args: '--static-decode=disas_m_nocp'),
  decodetree.process('a32.decode', extra_args: '--static-decode=disas_a32'),
  decodetree.process('a32-uncond.decode', extra_args: '--static-decode=disas_a32_uncond'),
  decodetree.process('t32.decode', extra_args: '--static-decode=disas_t32'),
  decodetree.process('t16.decode', extra_args: ['-w', '16', '--static-decode=disas_t16']),
]

arm_ss = ss.source_set()
arm_ss.add(gen)
arm_ss.add(files(
  'cpu.c',
  'crypto_helper.c',
  'debug_helper.c',
  'gdbstub.c',
  'helper.c',
  'iwmmxt_helper.c',
  'm_helper.c',
  'neon_helper.c',
  'op_helper.c',
  'tlb_helper.c',
  'translate.c',
  'vec_helper.c',
  'vfp_helper.c',
  'cpu_tcg.c',
))
arm_ss.add(zlib)

arm_ss.add(when: 'CONFIG_KVM', if_true: files('kvm.c', 'kvm64.c'), if_false: files('kvm-stub.c'))

arm_ss.add(when: 'TARGET_AARCH64', if_true: files(
  'cpu64.c',
  'gdbstub64.c',
  'helper-a64.c',
  'mte_helper.c',
  'pauth_helper.c',
  'sve_helper.c',
  'translate-a64.c',
  'translate-sve.c',
))

arm_softmmu_ss = ss.source_set()
arm_softmmu_ss.add(files(
  'arch_dump.c',
  'arm-powerctl.c',
  'machine.c',
  'monitor.c',
  'psci.c',
))

target_arch += {'arm': arm_ss}
target_softmmu_arch += {'arm': arm_softmmu_ss}
