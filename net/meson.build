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
  'net-hmp-cmds.c',
  'net.c',
  'queue.c',
  'socket.c',
  'stream.c',
  'dgram.c',
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
if have_vhost_net_user
  softmmu_ss.add(when: 'CONFIG_VIRTIO_NET', if_true: files('vhost-user.c'), if_false: files('vhost-user-stub.c'))
  softmmu_ss.add(when: 'CONFIG_ALL', if_true: files('vhost-user-stub.c'))
endif

softmmu_ss.add(when: 'CONFIG_LINUX', if_true: files('tap-linux.c'))
softmmu_ss.add(when: 'CONFIG_BSD', if_true: files('tap-bsd.c'))
softmmu_ss.add(when: 'CONFIG_SOLARIS', if_true: files('tap-solaris.c'))
tap_posix = ['tap.c']
if not config_host.has_key('CONFIG_LINUX') and not config_host.has_key('CONFIG_BSD') and not config_host.has_key('CONFIG_SOLARIS')
  tap_posix += 'tap-stub.c'
endif
softmmu_ss.add(when: 'CONFIG_POSIX', if_true: files(tap_posix))
softmmu_ss.add(when: 'CONFIG_WIN32', if_true: files('tap-win32.c'))
if have_vhost_net_vdpa
  softmmu_ss.add(when: 'CONFIG_VIRTIO_NET', if_true: files('vhost-vdpa.c'), if_false: files('vhost-vdpa-stub.c'))
  softmmu_ss.add(when: 'CONFIG_ALL', if_true: files('vhost-vdpa-stub.c'))
endif

vmnet_files = files(
  'vmnet-common.m',
  'vmnet-bridged.m',
  'vmnet-host.c',
  'vmnet-shared.c'
)
softmmu_ss.add(when: vmnet, if_true: vmnet_files)
subdir('can')
