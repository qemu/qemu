arm_ss = ss.source_set()
arm_ss.add(files(
  'cpu.c',
  'crypto_helper.c',
  'debug_helper.c',
  'gdbstub.c',
  'helper.c',
  'iwmmxt_helper.c',
  'm_helper.c',
  'mve_helper.c',
  'neon_helper.c',
  'op_helper.c',
  'tlb_helper.c',
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
  'sme_helper.c',
))

arm_softmmu_ss = ss.source_set()
arm_softmmu_ss.add(files(
  'arch_dump.c',
  'arm-powerctl.c',
  'machine.c',
  'monitor.c',
  'psci.c',
  'ptw.c',
))

subdir('hvf')

if 'CONFIG_TCG' in config_all
   subdir('tcg')
endif

target_arch += {'arm': arm_ss}
target_softmmu_arch += {'arm': arm_softmmu_ss}
