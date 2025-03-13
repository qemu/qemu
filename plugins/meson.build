if not get_option('plugins')
  subdir_done()
endif

qemu_plugin_symbols = configure_file(
  input: files('../include/qemu/qemu-plugin.h'),
  output: 'qemu-plugin.symbols',
  capture: true,
  command: [files('../scripts/qemu-plugin-symbols.py'), '@INPUT@'])

# Modules need more symbols than just those in plugins/qemu-plugins.symbols
if not enable_modules
  if host_os == 'darwin'
    configure_file(
      input: qemu_plugin_symbols,
      output: 'qemu-plugins-ld64.symbols',
      capture: true,
      command: ['sed', '-ne', 's/^[[:space:]]*\\(qemu_.*\\);/_\\1/p', '@INPUT@'])
    emulator_link_args += ['-Wl,-exported_symbols_list,plugins/qemu-plugins-ld64.symbols']
  elif host_os == 'windows' and meson.get_compiler('c').get_id() == 'clang'
    # LLVM/lld does not support exporting specific symbols. However, it works
    # out of the box with dllexport/dllimport attribute we set in the code.
  else
    emulator_link_args += ['-Xlinker', '--dynamic-list=' + qemu_plugin_symbols.full_path()]
  endif
endif

if host_os == 'windows'
  # Generate a .lib file for plugins to link against.
  # First, create a .def file listing all the symbols a plugin should expect to have
  # available in qemu
  win32_plugin_def = configure_file(
    input: qemu_plugin_symbols,
    output: 'qemu_plugin_api.def',
    capture: true,
    command: ['sed', '-e', '0,/^/s//EXPORTS/; s/[{};]//g', '@INPUT@'])

  # then use dlltool to assemble a delaylib.
  # The delaylib will have an "imaginary" name (qemu.exe), that is used by the
  # linker file we add with plugins (win32_linker.c) to identify that we want
  # to find missing symbols in current program.
  win32_qemu_plugin_api_link_flags = ['-Lplugins', '-lqemu_plugin_api']
  if meson.get_compiler('c').get_id() == 'clang'
    # With LLVM/lld, delaylib is specified at link time (-delayload)
    dlltool = find_program('llvm-dlltool', required: true)
    dlltool_cmd = [dlltool, '-d', '@INPUT@', '-l', '@OUTPUT@', '-D', 'qemu.exe']
    win32_qemu_plugin_api_link_flags += ['-Wl,-delayload=qemu.exe']
  else
    # With gcc/ld, delay lib is built with a specific delay parameter.
    dlltool = find_program('dlltool', required: true)
    dlltool_cmd = [dlltool, '--input-def', '@INPUT@',
                   '--output-delaylib', '@OUTPUT@', '--dllname', 'qemu.exe']
  endif
  win32_qemu_plugin_api_lib = configure_file(
    input: win32_plugin_def,
    output: 'libqemu_plugin_api.a',
    command: dlltool_cmd
  )
endif

user_ss.add(files('user.c', 'api-user.c'))
system_ss.add(files('system.c', 'api-system.c'))

libuser_ss.add(files('api.c', 'core.c'))
libsystem_ss.add(files('api.c', 'core.c'))

common_ss.add(files('loader.c'))

