t = []
if get_option('plugins')
  foreach i : ['bb', 'empty', 'insn', 'mem', 'syscall']
    if targetos == 'windows'
      t += shared_module(i, files(i + '.c') + '../../contrib/plugins/win32_linker.c',
                        include_directories: '../../include/qemu',
                        objects: [win32_qemu_plugin_api_lib],
                        dependencies: glib)

    else
      t += shared_module(i, files(i + '.c'),
                        include_directories: '../../include/qemu',
                        dependencies: glib)
    endif
  endforeach
endif
if t.length() > 0
  alias_target('test-plugins', t)
else
  run_target('test-plugins', command: find_program('true'))
endif
