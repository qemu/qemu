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
), if_false: files('tcg-stub.c'))

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

# Newer kernels on s390 check for an S390_PGSTE program header and
# enable the pgste page table extensions in that case. This makes
# the vm.allocate_pgste sysctl unnecessary. We enable this program
# header if
#  - we build on s390x
#  - we build the system emulation for s390x (qemu-system-s390x)
#  - KVM is enabled
#  - the linker supports --s390-pgste
if host_machine.cpu_family() == 's390x' and cc.has_link_argument('-Wl,--s390-pgste')
  s390x_softmmu_ss.add(when: 'CONFIG_KVM',
                       if_true: declare_dependency(link_args: ['-Wl,--s390-pgste']))
endif

target_arch += {'s390x': s390x_ss}
target_softmmu_arch += {'s390x': s390x_softmmu_ss}
