gen = decodetree.process('insns.decode')

loongarch_ss = ss.source_set()
loongarch_ss.add(files(
  'cpu.c',
  'gdbstub.c',
))

loongarch_system_ss = ss.source_set()
loongarch_system_ss.add(files(
  'arch_dump.c',
  'cpu_helper.c',
  'csr.c',
  'loongarch-qmp-cmds.c',
  'machine.c',
))

common_ss.add(when: 'CONFIG_LOONGARCH_DIS', if_true: [files('disas.c'), gen])

subdir('tcg')

target_arch += {'loongarch': loongarch_ss}
target_system_arch += {'loongarch': loongarch_system_ss}
subdir('kvm')
