softmmu_ss.add(files(
  'announce.c',
  'checksum.c',
  'colo-compare.c',
  'colo.c',
  'dump.c',
  'eth.c',
  'filter-buffer.c',
  'filter-mirror.c',
  'filter-rewriter.c',
  'filter.c',
  'hub.c',
  'net.c',
  'queue.c',
  'socket.c',
  'util.c',
))

softmmu_ss.add(when: 'CONFIG_TCG', if_true: files('filter-replay.c'))

if have_l2tpv3
  softmmu_ss.add(files('l2tpv3.c'))
endif
softmmu_ss.add(when: slirp, if_true: files('slirp.c'))
softmmu_ss.add(when: vde, if_true: files('vde.c'))
if have_netmap
  softmmu_ss.add(files('netmap.c'))
endif
vhost_user_ss = ss.source_set()
vhost_user_ss.add(when: 'CONFIG_VIRTIO_NET', if_true: files('vhost-user.c'), if_false: files('vhost-user-stub.c'))
softmmu_ss.add_all(when: 'CONFIG_VHOST_NET_USER', if_true: vhost_user_ss)
softmmu_ss.add(when: 'CONFIG_ALL', if_true: files('vhost-user-stub.c'))

softmmu_ss.add(when: 'CONFIG_LINUX', if_true: files('tap-linux.c'))
softmmu_ss.add(when: 'CONFIG_BSD', if_true: files('tap-bsd.c'))
softmmu_ss.add(when: 'CONFIG_SOLARIS', if_true: files('tap-solaris.c'))
tap_posix = ['tap.c']
if not config_host.has_key('CONFIG_LINUX') and not config_host.has_key('CONFIG_BSD') and not config_host.has_key('CONFIG_SOLARIS')
  tap_posix += 'tap-stub.c'
endif
softmmu_ss.add(when: 'CONFIG_POSIX', if_true: files(tap_posix))
softmmu_ss.add(when: 'CONFIG_WIN32', if_true: files('tap-win32.c'))
softmmu_ss.add(when: 'CONFIG_VHOST_NET_VDPA', if_true: files('vhost-vdpa.c'))

subdir('can')
