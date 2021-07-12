s390x_ss = ss.source_set()
s390x_ss.add(files(
  'cpu.c',
  'cpu_features.c',
  'cpu_models.c',
  'gdbstub.c',
  'interrupt.c',
  'cpu-dump.c',
))

gen_features = executable('gen-features', 'gen-features.c', native: true,
                          build_by_default: false)

gen_features_h = custom_target('gen-features.h',
                               output: 'gen-features.h',
                               capture: true,
                               command: gen_features)

s390x_ss.add(gen_features_h)

s390x_softmmu_ss = ss.source_set()
s390x_softmmu_ss.add(files(
  'helper.c',
  'arch_dump.c',
  'diag.c',
  'ioinst.c',
  'machine.c',
  'mmu_helper.c',
  'sigp.c',
  'cpu-sysemu.c',
  'cpu_models_sysemu.c',
))

s390x_user_ss = ss.source_set()
s390x_user_ss.add(files(
  'cpu_models_user.c',
))

subdir('tcg')
subdir('kvm')

target_arch += {'s390x': s390x_ss}
target_softmmu_arch += {'s390x': s390x_softmmu_ss}
target_user_arch += {'s390x': s390x_user_ss}
