executable('virtiofsd', files(
  'buffer.c',
  'fuse_opt.c',
  'fuse_log.c',
  'fuse_lowlevel.c',
  'fuse_signals.c',
  'fuse_virtio.c',
  'helper.c',
  'passthrough_ll.c',
  'passthrough_seccomp.c'),
  link_with: libvhost_user,
  dependencies: [seccomp, qemuutil, libcap_ng],
  install: true,
  install_dir: get_option('libexecdir'))

configure_file(input: '50-qemu-virtiofsd.json.in',
               output: '50-qemu-virtiofsd.json',
               configuration: { 'libexecdir' : get_option('prefix') / get_option('libexecdir') },
               install_dir: qemu_datadir / 'vhost-user')
