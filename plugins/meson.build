plugin_ldflags = []
# Modules need more symbols than just those in plugins/qemu-plugins.symbols
if not enable_modules
  if targetos == 'darwin'
    configure_file(
      input: files('qemu-plugins.symbols'),
      output: 'qemu-plugins-ld64.symbols',
      capture: true,
      command: ['sed', '-ne', 's/^[[:space:]]*\\(qemu_.*\\);/_\\1/p', '@INPUT@'])
    plugin_ldflags = ['-Wl,-exported_symbols_list,plugins/qemu-plugins-ld64.symbols']
  else
    plugin_ldflags = ['-Xlinker', '--dynamic-list=' + (meson.project_source_root() / 'plugins/qemu-plugins.symbols')]
  endif
endif

specific_ss.add(when: 'CONFIG_PLUGIN', if_true: [files(
  'loader.c',
  'core.c',
  'api.c',
), declare_dependency(link_args: plugin_ldflags)])
