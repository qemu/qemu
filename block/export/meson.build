blockdev_ss.add(files('export.c'))
blockdev_ss.add(when: ['CONFIG_LINUX', 'CONFIG_VHOST_USER'], if_true: files('vhost-user-blk-server.c'))
