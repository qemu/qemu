project('qemu', ['c'], meson_version: '>=0.55.0',
        default_options: ['warning_level=1', 'c_std=gnu99', 'cpp_std=gnu++11', 'b_lundef=false'],
        version: run_command('head', meson.source_root() / 'VERSION').stdout().strip())

not_found = dependency('', required: false)
keyval = import('unstable-keyval')
config_host = keyval.load(meson.current_build_dir() / 'config-host.mak')

add_project_arguments(config_host['QEMU_CFLAGS'].split(),
                      native: false, language: ['c', 'objc'])
add_project_arguments(config_host['QEMU_CXXFLAGS'].split(),
                      native: false, language: 'cpp')
add_project_link_arguments(config_host['QEMU_LDFLAGS'].split(),
                           native: false, language: ['c', 'cpp', 'objc'])
add_project_arguments(config_host['QEMU_INCLUDES'].split(),
                      language: ['c', 'cpp', 'objc'])

python = import('python').find_installation()

link_language = meson.get_external_property('link_language', 'cpp')
if link_language == 'cpp'
  add_languages('cpp', required: true, native: false)
endif
if host_machine.system() == 'darwin'
  add_languages('objc', required: false, native: false)
endif

configure_file(input: files('scripts/ninjatool.py'),
               output: 'ninjatool',
               configuration: config_host)
