project('libvhost-user', 'c',
        license: 'GPL-2.0-or-later',
        default_options: ['c_std=gnu99'])

threads = dependency('threads')
glib = dependency('glib-2.0')
inc = include_directories('../../include', '../../linux-headers')

vhost_user = static_library('vhost-user',
                            files('libvhost-user.c'),
                            include_directories: inc,
                            dependencies: threads,
                            c_args: '-D_GNU_SOURCE')

executable('link-test', files('link-test.c'),
           link_whole: vhost_user,
           include_directories: inc)

vhost_user_glib = static_library('vhost-user-glib',
                                 files('libvhost-user-glib.c'),
                                 include_directories: inc,
                                 link_with: vhost_user,
                                 dependencies: glib)

vhost_user_dep = declare_dependency(link_with: vhost_user_glib,
                                    dependencies: glib,
                                    include_directories: include_directories('.'))
