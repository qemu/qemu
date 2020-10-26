libvhost_user = static_library('vhost-user',
                               files('libvhost-user.c', 'libvhost-user-glib.c'),
                               build_by_default: false)
vhost_user = declare_dependency(link_with: libvhost_user)
