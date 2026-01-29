if have_tools and have_vhost_user and host_os == 'linux'
  executable('vhost-user-bridge', files('vhost-user-bridge.c'),
             dependencies: [qemuutil, vhost_user], install: false)
endif
