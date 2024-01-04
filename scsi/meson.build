block_ss.add(files('utils.c'))
if host_os == 'linux'
  block_ss.add(files('pr-manager.c', 'pr-manager-helper.c'))
else
  block_ss.add(files('pr-manager-stub.c'))
endif
