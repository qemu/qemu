executable('vhost-user-blk', files('vhost-user-blk.c'),
           dependencies: [qemuutil, vhost_user],
           build_by_default: targetos == 'linux',
           install: false)
