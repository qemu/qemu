if 'CONFIG_TOOLS' in config_host and virgl.found() and gbm.found() \
    and 'CONFIG_LINUX' in config_host and pixman.found()
  executable('vhost-user-gpu', files('vhost-user-gpu.c', 'virgl.c', 'vugbm.c'),
             dependencies: [qemuutil, pixman, gbm, virgl, vhost_user, opengl],
             install: true,
             install_dir: get_option('libexecdir'))

  configure_file(input: '50-qemu-gpu.json.in',
                 output: '50-qemu-gpu.json',
                 configuration: { 'libexecdir' : get_option('prefix') / get_option('libexecdir') },
                 install_dir: qemu_datadir / 'vhost-user')
endif
