executable('vhost-user-input', files('main.c'),
           dependencies: [qemuutil, vhost_user],
           build_by_default: host_os == 'linux',
           install: false)
