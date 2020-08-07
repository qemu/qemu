ppc_ss = ss.source_set()
ppc_ss.add(files(
  'cpu-models.c',
  'cpu.c',
  'dfp_helper.c',
  'excp_helper.c',
  'fpu_helper.c',
  'gdbstub.c',
  'int_helper.c',
  'mem_helper.c',
  'misc_helper.c',
  'timebase_helper.c',
  'translate.c',
))

ppc_ss.add(libdecnumber)

ppc_ss.add(when: 'CONFIG_KVM', if_true: files('kvm.c'), if_false: files('kvm-stub.c'))
ppc_ss.add(when: 'CONFIG_USER_ONLY', if_true: files('user_only_helper.c'))

ppc_softmmu_ss = ss.source_set()
ppc_softmmu_ss.add(files(
  'arch_dump.c',
  'machine.c',
  'mmu-hash32.c',
  'mmu_helper.c',
  'monitor.c',
))
ppc_softmmu_ss.add(when: 'TARGET_PPC64', if_true: files(
  'compat.c',
  'mmu-book3s-v3.c',
  'mmu-hash64.c',
  'mmu-radix64.c',
))

target_arch += {'ppc': ppc_ss}
target_softmmu_arch += {'ppc': ppc_softmmu_ss}
