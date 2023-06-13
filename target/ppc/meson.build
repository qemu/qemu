ppc_ss = ss.source_set()
ppc_ss.add(files(
  'cpu-models.c',
  'cpu.c',
  'cpu_init.c',
  'excp_helper.c',
  'gdbstub.c',
  'helper_regs.c',
))

ppc_ss.add(when: 'CONFIG_TCG', if_true: files(
  'dfp_helper.c',
  'fpu_helper.c',
  'int_helper.c',
  'mem_helper.c',
  'misc_helper.c',
  'timebase_helper.c',
  'translate.c',
  'power8-pmu.c',
))

ppc_ss.add(libdecnumber)

gen = [
  decodetree.process('insn32.decode',
                     extra_args: '--static-decode=decode_insn32'),
  decodetree.process('insn64.decode',
                     extra_args: ['--static-decode=decode_insn64',
                                  '--insnwidth=64']),
]
ppc_ss.add(gen)

ppc_ss.add(when: 'CONFIG_KVM', if_true: files('kvm.c'), if_false: files('kvm-stub.c'))
ppc_ss.add(when: 'CONFIG_USER_ONLY', if_true: files('user_only_helper.c'))

ppc_system_ss = ss.source_set()
ppc_system_ss.add(files(
  'arch_dump.c',
  'machine.c',
  'mmu-hash32.c',
  'mmu_common.c',
  'ppc-qmp-cmds.c',
))
ppc_system_ss.add(when: 'CONFIG_TCG', if_true: files(
  'mmu_helper.c',
), if_false: files(
  'tcg-stub.c',
))

ppc_system_ss.add(when: 'TARGET_PPC64', if_true: files(
  'compat.c',
  'mmu-book3s-v3.c',
  'mmu-hash64.c',
  'mmu-radix64.c',
))

target_arch += {'ppc': ppc_ss}
target_softmmu_arch += {'ppc': ppc_system_ss}
