blockdev_ss.add(files('export.c'))

if have_vhost_user_blk_server
    blockdev_ss.add(files('vhost-user-blk-server.c', 'virtio-blk-handler.c'))
endif

blockdev_ss.add(when: fuse, if_true: files('fuse.c'))
