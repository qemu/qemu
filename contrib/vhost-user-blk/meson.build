# FIXME: broken on 32-bit architectures
executable('vhost-user-blk', files('vhost-user-blk.c'),
           link_with: libvhost_user,
           dependencies: qemuutil,
           build_by_default: false,
           install: false)
