gen = [
  decodetree.process('insn.decode', extra_args: [ '--decode', 'decode_insn',
                                                  '--insnwidth', '16' ])
]

avr_ss = ss.source_set()
avr_system_ss = ss.source_set()

avr_ss.add(gen)
avr_ss.add(files(
  'translate.c',
  'helper.c',
  'cpu.c',
  'gdbstub.c',
  'disas.c'))

avr_system_ss.add(files('machine.c'))

target_arch += {'avr': avr_ss}
target_system_arch += {'avr': avr_system_ss}
