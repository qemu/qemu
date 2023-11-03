fsdev_ss = ss.source_set()
fsdev_ss.add(files('qemu-fsdev-opts.c', 'qemu-fsdev-throttle.c'))
fsdev_ss.add(when: ['CONFIG_FSDEV_9P'], if_true: files(
  '9p-iov-marshal.c',
  '9p-marshal.c',
  'qemu-fsdev.c',
), if_false: files('qemu-fsdev-dummy.c'))
if host_os in ['linux', 'darwin']
  system_ss.add_all(fsdev_ss)
endif

if have_virtfs_proxy_helper
  executable('virtfs-proxy-helper',
             files('virtfs-proxy-helper.c', '9p-marshal.c', '9p-iov-marshal.c'),
             dependencies: [qemuutil, libattr, libcap_ng],
             install: true,
             install_dir: get_option('libexecdir'))
endif
