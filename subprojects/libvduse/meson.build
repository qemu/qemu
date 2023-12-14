project('libvduse', 'c',
        license: 'GPL-2.0-or-later',
        default_options: ['warning_level=1', 'c_std=gnu99'])

cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments('-Wsign-compare',
                                                 '-Wdeclaration-after-statement',
                                                 '-Wstrict-aliasing'),
                      native: false, language: 'c')

libvduse = static_library('vduse',
                          files('libvduse.c'),
                          c_args: '-D_GNU_SOURCE')

libvduse_dep = declare_dependency(link_with: libvduse,
                                  include_directories: include_directories('.'))
