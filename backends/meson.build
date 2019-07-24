softmmu_ss.add([files(
  'cryptodev-builtin.c',
  'cryptodev.c',
  'hostmem-ram.c',
  'hostmem.c',
  'rng-builtin.c',
  'rng-egd.c',
  'rng.c',
), numa])

softmmu_ss.add(when: 'CONFIG_POSIX', if_true: files('rng-random.c'))
softmmu_ss.add(when: 'CONFIG_POSIX', if_true: files('hostmem-file.c'))
softmmu_ss.add(when: 'CONFIG_LINUX', if_true: files('hostmem-memfd.c'))
softmmu_ss.add(when: ['CONFIG_VHOST_USER', 'CONFIG_VIRTIO'], if_true: files('vhost-user.c'))
softmmu_ss.add(when: 'CONFIG_VIRTIO_CRYPTO', if_true: files('cryptodev-vhost.c'))
softmmu_ss.add(when: ['CONFIG_VIRTIO_CRYPTO', 'CONFIG_VHOST_CRYPTO'], if_true: files('cryptodev-vhost-user.c'))
softmmu_ss.add(when: 'CONFIG_GIO', if_true: [files('dbus-vmstate.c'), gio])

subdir('tpm')
