project('libvduse', 'c',
        license: 'GPL-2.0-or-later',
        default_options: ['c_std=gnu99'])

libvduse = static_library('vduse',
                          files('libvduse.c'),
                          c_args: '-D_GNU_SOURCE')

libvduse_dep = declare_dependency(link_with: libvduse,
                                  include_directories: include_directories('.'))
