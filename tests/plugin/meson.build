t = []
foreach i : ['bb', 'empty', 'insn', 'mem', 'syscall']
  t += shared_module(i, files(i + '.c'),
                     include_directories: '../../include/qemu',
                     dependencies: glib)
endforeach
alias_target('test-plugins', t)
