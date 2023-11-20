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

if get_option('plugins')
  if targetos == 'windows'
    dlltool = find_program('dlltool', required: true)

    # Generate a .lib file for plugins to link against.
    # First, create a .def file listing all the symbols a plugin should expect to have
    # available in qemu
    win32_plugin_def = configure_file(
      input: files('qemu-plugins.symbols'),
      output: 'qemu_plugin_api.def',
      capture: true,
      command: ['sed', '-e', '0,/^/s//EXPORTS/; s/[{};]//g', '@INPUT@'])
    # then use dlltool to assemble a delaylib.
    win32_qemu_plugin_api_lib = configure_file(
      input: win32_plugin_def,
      output: 'libqemu_plugin_api.a',
      command: [dlltool, '--input-def', '@INPUT@',
                '--output-delaylib', '@OUTPUT@', '--dllname', 'qemu.exe']
    )
  endif
  specific_ss.add(files(
    'loader.c',
    'core.c',
    'api.c',
  ), declare_dependency(link_args: plugin_ldflags))
endif
