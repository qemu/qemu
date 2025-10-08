arm_ss = ss.source_set()
arm_common_ss = ss.source_set()
arm_ss.add(files(
  'gdbstub.c',
))

arm_ss.add(when: 'TARGET_AARCH64', if_true: files(
  'cpu64.c',
  'gdbstub64.c'
))

arm_common_ss.add(files(
  'mmuidx.c',
))

arm_system_ss = ss.source_set()
arm_common_system_ss = ss.source_set()
arm_system_ss.add(files(
  'arm-qmp-cmds.c',
))
arm_system_ss.add(when: 'CONFIG_KVM', if_true: files('hyp_gdbstub.c', 'kvm.c'))
arm_system_ss.add(when: 'CONFIG_HVF', if_true: files('hyp_gdbstub.c'))

arm_user_ss = ss.source_set()
arm_user_ss.add(files('cpu.c'))
arm_user_ss.add(when: 'TARGET_AARCH64', if_false: files(
  'cpu32-stubs.c',
))
arm_user_ss.add(files(
  'cpregs-gcs.c',
  'cpregs-pmu.c',
  'debug_helper.c',
  'helper.c',
  'vfp_fpscr.c',
  'el2-stubs.c',
))
arm_user_ss.add(when: 'CONFIG_ARM_COMPATIBLE_SEMIHOSTING',
		        if_true: files('common-semi-target.c'))

arm_common_system_ss.add(files('cpu.c'))
arm_common_system_ss.add(when: 'TARGET_AARCH64', if_false: files(
  'cpu32-stubs.c'))
arm_common_system_ss.add(when: 'CONFIG_KVM', if_false: files('kvm-stub.c'))
arm_common_system_ss.add(when: 'CONFIG_HVF', if_false: files('hvf-stub.c'))
arm_common_system_ss.add(when: 'CONFIG_ARM_COMPATIBLE_SEMIHOSTING',
		                 if_true: files('common-semi-target.c'))
arm_common_system_ss.add(files(
  'arch_dump.c',
  'arm-powerctl.c',
  'cortex-regs.c',
  'cpregs-gcs.c',
  'cpregs-pmu.c',
  'cpu-irq.c',
  'debug_helper.c',
  'helper.c',
  'machine.c',
  'ptw.c',
  'vfp_fpscr.c',
))

subdir('hvf')

if 'CONFIG_TCG' in config_all_accel
   subdir('tcg')
else
    arm_common_system_ss.add(files('tcg-stubs.c'))
endif

target_arch += {'arm': arm_ss}
target_system_arch += {'arm': arm_system_ss}
target_user_arch += {'arm': arm_user_ss}
target_common_arch += {'arm': arm_common_ss}
target_common_system_arch += {'arm': arm_common_system_ss}
