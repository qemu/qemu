t = []
if get_option('plugins')
  foreach i : ['bb', 'discons', 'empty', 'inline', 'insn', 'mem', 'reset', 'syscall', 'patch']
    t += shared_module(i, files(i + '.c'),
                       dependencies: plugins_deps)
  endforeach
endif
if t.length() > 0
  alias_target('test-plugins', t)
else
  run_target('test-plugins', command: [python, '-c', ''])
endif

plugin_modules += t
