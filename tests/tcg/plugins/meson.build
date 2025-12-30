test_plugins = ['bb.c', 'discons.c', 'empty.c', 'inline.c', 'insn.c', 'mem.c',
                'reset.c', 'syscall.c', 'patch.c']

t = []
if get_option('plugins')
  foreach i : test_plugins
    t += shared_module(fs.stem(i), files(i),
                       dependencies: plugins_deps)
  endforeach
endif
if t.length() > 0
  alias_target('test-plugins', t)
else
  run_target('test-plugins', command: [python, '-c', ''])
endif

plugin_modules += t
