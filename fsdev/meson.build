have_virtfs_proxy_helper = have_tools and libattr.found() and libcap_ng.found() and 'CONFIG_VIRTFS' in config_host
if have_virtfs_proxy_helper
  executable('virtfs-proxy-helper',
             files('virtfs-proxy-helper.c', '9p-marshal.c', '9p-iov-marshal.c'),
             dependencies: [qemuutil, libattr, libcap_ng],
             install: true,
             install_dir: get_option('libexecdir'))
endif
