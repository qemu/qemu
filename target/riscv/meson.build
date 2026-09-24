# FIXME extra_args should accept files()
gen = [
  decodetree.process('insn16.decode', extra_args: ['--static-decode=decode_insn16', '--insnwidth=16']),
  decodetree.process('insn32.decode', extra_args: '--static-decode=decode_insn32'),
  decodetree.process('xthead.decode', extra_args: '--static-decode=decode_xthead'),
  decodetree.process('XVentanaCondOps.decode', extra_args: '--static-decode=decode_XVentanaCodeOps'),
  decodetree.process('xmips.decode', extra_args: '--static-decode=decode_xmips'),
  decodetree.process('xlrbr.decode', extra_args: '--static-decode=decode_xlrbr'),
]

riscv_ss = ss.source_set()
riscv_ss.add(gen)

riscv_ss.add(when: 'CONFIG_ARM_COMPATIBLE_SEMIHOSTING',
		     if_true: files('common-semi-target.c'))

riscv_ss.add(files(
  'cpu.c',
  'gdbstub.c',
))

riscv_stubs_ss = ss.source_set()

riscv_system_ss = ss.source_set()
riscv_system_ss.add(files(
  'arch_dump.c',
  'monitor.c',
  'machine.c',
  'time_helper.c',
  'riscv-qmp-cmds.c',
))

subdir('tcg')
subdir('kvm')

target_arch += {'riscv': riscv_ss}
target_system_arch += {'riscv': riscv_system_ss}
target_stubs_arch += {'riscv': riscv_stubs_ss}
