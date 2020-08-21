executable('vhost-user-input', files('main.c'),
           link_with: libvhost_user,
           dependencies: qemuutil,
           build_by_default: targetos == 'linux',
           install: false)
