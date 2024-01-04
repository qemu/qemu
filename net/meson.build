system_ss.add(files(
  'announce.c',
  'checksum.c',
  'dump.c',
  'eth.c',
  'filter-buffer.c',
  'filter-mirror.c',
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

if get_option('replication').allowed() or \
    get_option('colo_proxy').allowed()
  system_ss.add(files('colo-compare.c'))
  system_ss.add(files('colo.c'))
endif

if get_option('colo_proxy').allowed()
  system_ss.add(files('filter-rewriter.c'))
endif

system_ss.add(when: 'CONFIG_TCG', if_true: files('filter-replay.c'))

if have_l2tpv3
  system_ss.add(files('l2tpv3.c'))
endif
system_ss.add(when: slirp, if_true: files('slirp.c'))
system_ss.add(when: vde, if_true: files('vde.c'))
if have_netmap
  system_ss.add(files('netmap.c'))
endif

system_ss.add(when: libxdp, if_true: files('af-xdp.c'))

if have_vhost_net_user
  system_ss.add(when: 'CONFIG_VIRTIO_NET', if_true: files('vhost-user.c'), if_false: files('vhost-user-stub.c'))
endif

if host_os == 'windows'
  system_ss.add(files('tap-win32.c'))
elif host_os == 'linux'
  system_ss.add(files('tap.c', 'tap-linux.c'))
elif host_os in bsd_oses
  system_ss.add(files('tap.c', 'tap-bsd.c'))
elif host_os == 'sunos'
  system_ss.add(files('tap.c', 'tap-solaris.c'))
else
  system_ss.add(files('tap.c', 'tap-stub.c'))
endif
if have_vhost_net_vdpa
  system_ss.add(when: 'CONFIG_VIRTIO_NET', if_true: files('vhost-vdpa.c'), if_false: files('vhost-vdpa-stub.c'))
endif

vmnet_files = files(
  'vmnet-common.m',
  'vmnet-bridged.m',
  'vmnet-host.c',
  'vmnet-shared.c'
)
system_ss.add(when: vmnet, if_true: vmnet_files)
subdir('can')
