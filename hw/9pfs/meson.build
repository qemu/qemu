fs_ss = ss.source_set()
fs_ss.add(files(
  '9p-local.c',
  '9p-posix-acl.c',
  '9p-proxy.c',
  '9p-synth.c',
  '9p-xattr-user.c',
  '9p-xattr.c',
  '9p.c',
  'codir.c',
  'cofile.c',
  'cofs.c',
  'coth.c',
  'coxattr.c',
))
if host_os == 'darwin'
  fs_ss.add(files('9p-util-darwin.c'))
elif host_os == 'linux'
  fs_ss.add(files('9p-util-linux.c'))
endif
fs_ss.add(when: 'CONFIG_XEN_BUS', if_true: files('xen-9p-backend.c'))
system_ss.add_all(when: 'CONFIG_FSDEV_9P', if_true: fs_ss)

specific_ss.add(when: 'CONFIG_VIRTIO_9P', if_true: files('virtio-9p-device.c'))
