arm_ss = ss.source_set()
arm_ss.add(files(
  'cpu.c',
  'debug_helper.c',
  'gdbstub.c',
  'helper.c',
  'vfp_helper.c',
  'cpu_tcg.c',
))
arm_ss.add(zlib)

arm_ss.add(when: 'CONFIG_KVM', if_true: files('kvm.c', 'kvm64.c'), if_false: files('kvm-stub.c'))

arm_ss.add(when: 'TARGET_AARCH64', if_true: files(
  'cpu64.c',
  'gdbstub64.c',
))

arm_softmmu_ss = ss.source_set()
arm_softmmu_ss.add(files(
  'arch_dump.c',
  'arm-powerctl.c',
  'arm-qmp-cmds.c',
  'machine.c',
  'ptw.c',
))

subdir('hvf')

if 'CONFIG_TCG' in config_all
   subdir('tcg')
else
    arm_ss.add(files('tcg-stubs.c'))
endif

target_arch += {'arm': arm_ss}
target_softmmu_arch += {'arm': arm_softmmu_ss}
