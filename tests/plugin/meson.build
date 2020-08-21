t = []
foreach i : ['bb', 'empty', 'insn', 'mem', 'hotblocks', 'howvec', 'hotpages', 'lockstep']
  t += shared_module(i, files(i + '.c'),
                     include_directories: '../../include/qemu',
                     dependencies: glib)
endforeach
alias_target('plugins', t)
