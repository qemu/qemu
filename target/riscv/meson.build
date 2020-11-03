# FIXME extra_args should accept files()
dir = meson.current_source_dir()
gen32 = [
  decodetree.process('insn16.decode', extra_args: [dir / 'insn16-32.decode', '--static-decode=decode_insn16', '--insnwidth=16']),
  decodetree.process('insn32.decode', extra_args: '--static-decode=decode_insn32'),
]

gen64 = [
  decodetree.process('insn16.decode', extra_args: [dir / 'insn16-64.decode', '--static-decode=decode_insn16', '--insnwidth=16']),
  decodetree.process('insn32.decode', extra_args: [dir / 'insn32-64.decode', '--static-decode=decode_insn32']),
]

riscv_ss = ss.source_set()
riscv_ss.add(when: 'TARGET_RISCV32', if_true: gen32)
riscv_ss.add(when: 'TARGET_RISCV64', if_true: gen64)
riscv_ss.add(files(
  'cpu.c',
  'cpu_helper.c',
  'csr.c',
  'fpu_helper.c',
  'gdbstub.c',
  'op_helper.c',
  'vector_helper.c',
  'translate.c',
))

riscv_softmmu_ss = ss.source_set()
riscv_softmmu_ss.add(files(
  'pmp.c',
  'monitor.c',
  'machine.c'
))

target_arch += {'riscv': riscv_ss}
target_softmmu_arch += {'riscv': riscv_softmmu_ss}
