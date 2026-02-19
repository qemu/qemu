gen = decodetree.process('insns.decode')

or1k_ss = ss.source_set()
or1k_ss.add(gen)
or1k_ss.add(files(
  'cpu.c',
  'disas.c',
  'exception.c',
  'exception_helper.c',
  'fpu_helper.c',
  'gdbstub.c',
  'interrupt_helper.c',
  'sys_helper.c',
  'translate.c',
))

or1k_common_system_ss = ss.source_set()
or1k_common_system_ss.add(files(
  'interrupt.c',
  'machine.c',
  'mmu.c',
))

target_arch += {'or1k': or1k_ss}
target_common_system_arch += {'or1k': or1k_common_system_ss}
