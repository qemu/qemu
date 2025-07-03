system_ss.add([files(
  'cryptodev-builtin.c',
  'cryptodev-hmp-cmds.c',
  'cryptodev.c',
  'hostmem-ram.c',
  'hostmem.c',
  'rng-builtin.c',
  'rng-egd.c',
  'rng.c',
  'confidential-guest-support.c',
), numa])

if host_os != 'windows'
  system_ss.add(files('rng-random.c'))
  if host_os != 'emscripten'
    system_ss.add(files('hostmem-file.c'))
    system_ss.add([files('hostmem-shm.c'), rt])
  endif
endif
if host_os == 'linux'
  system_ss.add(files('hostmem-memfd.c'))
  system_ss.add(files('host_iommu_device.c'))
endif
if keyutils.found()
    system_ss.add(keyutils, files('cryptodev-lkcf.c'))
endif
if have_vhost_user
  system_ss.add(when: 'CONFIG_VIRTIO', if_true: files('vhost-user.c'))
endif
system_ss.add(when: 'CONFIG_VIRTIO_CRYPTO', if_true: files('cryptodev-vhost.c'))
system_ss.add(when: 'CONFIG_IOMMUFD', if_true: files('iommufd.c'))
if have_vhost_user_crypto
  system_ss.add(when: 'CONFIG_VIRTIO_CRYPTO', if_true: files('cryptodev-vhost-user.c'))
endif
system_ss.add(when: gio, if_true: files('dbus-vmstate.c'))
system_ss.add(when: 'CONFIG_SGX', if_true: files('hostmem-epc.c'))
if igvm.found()
  system_ss.add(igvm)
  system_ss.add(files('igvm-cfg.c'), igvm)
  system_ss.add(files('igvm.c'), igvm)
endif

system_ss.add(when: 'CONFIG_SPDM_SOCKET', if_true: files('spdm-socket.c'))

subdir('tpm')
