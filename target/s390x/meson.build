s390x_ss = ss.source_set()
s390x_ss.add(files(
  'cpu.c',
  'cpu_features.c',
  'cpu_models.c',
  'gdbstub.c',
  'helper.c',
  'interrupt.c',
))

s390x_ss.add(when: 'CONFIG_TCG', if_true: files(
  'cc_helper.c',
  'crypto_helper.c',
  'excp_helper.c',
  'fpu_helper.c',
  'int_helper.c',
  'mem_helper.c',
  'misc_helper.c',
  'translate.c',
  'vec_fpu_helper.c',
  'vec_helper.c',
  'vec_int_helper.c',
  'vec_string_helper.c',
), if_false: 'tcg-stub.c')

s390x_ss.add(when: 'CONFIG_KVM', if_true: files('kvm.c'), if_false: files('kvm-stub.c'))

gen_features = executable('gen-features', 'gen-features.c', native: true,
                          build_by_default: false)

gen_features_h = custom_target('gen-features.h',
                               output: 'gen-features.h',
                               capture: true,
                               command: gen_features)

s390x_ss.add(gen_features_h)

s390x_softmmu_ss = ss.source_set()
s390x_softmmu_ss.add(files(
  'arch_dump.c',
  'diag.c',
  'ioinst.c',
  'machine.c',
  'mmu_helper.c',
  'sigp.c',
))

target_arch += {'s390x': s390x_ss}
target_softmmu_arch += {'s390x': s390x_softmmu_ss}
