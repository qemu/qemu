# FIXME extra_args should accept files()
gen = [
  decodetree.process('insn16.decode', extra_args: ['--static-decode=decode_insn16', '--insnwidth=16']),
  decodetree.process('insn32.decode', extra_args: '--static-decode=decode_insn32'),
  decodetree.process('xthead.decode', extra_args: '--static-decode=decode_xthead'),
  decodetree.process('XVentanaCondOps.decode', extra_args: '--static-decode=decode_XVentanaCodeOps'),
]

riscv_ss = ss.source_set()
riscv_ss.add(gen)
riscv_ss.add(files(
  'cpu.c',
  'cpu_helper.c',
  'csr.c',
  'fpu_helper.c',
  'gdbstub.c',
  'op_helper.c',
  'vector_helper.c',
  'bitmanip_helper.c',
  'translate.c',
  'm128_helper.c',
  'crypto_helper.c'
))
riscv_ss.add(when: 'CONFIG_KVM', if_true: files('kvm.c'), if_false: files('kvm-stub.c'))

riscv_softmmu_ss = ss.source_set()
riscv_softmmu_ss.add(files(
  'arch_dump.c',
  'pmp.c',
  'debug.c',
  'monitor.c',
  'machine.c',
  'pmu.c',
  'time_helper.c'
))

target_arch += {'riscv': riscv_ss}
target_softmmu_arch += {'riscv': riscv_softmmu_ss}
