gen = [
  decodetree.process('insns.decode', extra_args: [ '--varinsnwidth', '32' ])
]

rx_ss = ss.source_set()
rx_ss.add(gen)
rx_ss.add(files(
  'translate.c',
  'op_helper.c',
  'helper.c',
  'cpu.c',
  'gdbstub.c',
  'disas.c'))

target_arch += {'rx': rx_ss}
target_system_arch += {'rx': ss.source_set()}
