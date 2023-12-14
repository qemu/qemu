project('libvhost-user', 'c',
        license: 'GPL-2.0-or-later',
        default_options: ['warning_level=1', 'c_std=gnu99'])

cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments('-Wsign-compare',
                                                 '-Wdeclaration-after-statement',
                                                 '-Wstrict-aliasing'),
                      native: false, language: 'c')

threads = dependency('threads')
glib = dependency('glib-2.0')

vhost_user = static_library('vhost-user',
                            files('libvhost-user.c'),
                            dependencies: threads,
                            c_args: '-D_GNU_SOURCE')

executable('link-test', files('link-test.c'),
           link_whole: vhost_user)

vhost_user_glib = static_library('vhost-user-glib',
                                 files('libvhost-user-glib.c'),
                                 link_with: vhost_user,
                                 dependencies: glib)

vhost_user_dep = declare_dependency(link_with: vhost_user_glib,
                                    dependencies: glib,
                                    include_directories: include_directories('.'))
