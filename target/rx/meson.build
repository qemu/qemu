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
  'disas.c'))

rx_common_system_ss = ss.source_set()
rx_common_system_ss.add(files('gdbstub.c'))

target_arch += {'rx': rx_ss}
target_common_system_arch += {'rx': rx_common_system_ss}
