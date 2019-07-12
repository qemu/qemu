if 'CONFIG_CURL' in config_host
  executable('elf2dmp', files('main.c', 'addrspace.c', 'download.c', 'pdb.c', 'qemu_elf.c'),
             dependencies: [glib, curl],
             install: true)
endif
