softmmu_virtio_ss = ss.source_set()
softmmu_virtio_ss.add(files('virtio-bus.c'))
softmmu_virtio_ss.add(when: 'CONFIG_VIRTIO_PCI', if_true: files('virtio-pci.c'))
softmmu_virtio_ss.add(when: 'CONFIG_VIRTIO_MMIO', if_true: files('virtio-mmio.c'))

specific_virtio_ss = ss.source_set()
specific_virtio_ss.add(files('virtio.c'))
specific_virtio_ss.add(files('virtio-config-io.c', 'virtio-qmp.c'))

if have_vhost
  specific_virtio_ss.add(files('vhost.c', 'vhost-backend.c', 'vhost-iova-tree.c'))
  if have_vhost_user
    specific_virtio_ss.add(files('vhost-user.c'))
  endif
  if have_vhost_vdpa
    specific_virtio_ss.add(files('vhost-vdpa.c', 'vhost-shadow-virtqueue.c'))
  endif
else
  softmmu_virtio_ss.add(files('vhost-stub.c'))
endif

specific_virtio_ss.add(when: 'CONFIG_VIRTIO_BALLOON', if_true: files('virtio-balloon.c'))
specific_virtio_ss.add(when: 'CONFIG_VIRTIO_CRYPTO', if_true: files('virtio-crypto.c'))
specific_virtio_ss.add(when: 'CONFIG_VHOST_USER_FS', if_true: files('vhost-user-fs.c'))
specific_virtio_ss.add(when: 'CONFIG_VIRTIO_PMEM', if_true: files('virtio-pmem.c'))
specific_virtio_ss.add(when: 'CONFIG_VHOST_VSOCK', if_true: files('vhost-vsock.c', 'vhost-vsock-common.c'))
specific_virtio_ss.add(when: 'CONFIG_VHOST_USER_VSOCK', if_true: files('vhost-user-vsock.c', 'vhost-vsock-common.c'))
specific_virtio_ss.add(when: 'CONFIG_VIRTIO_RNG', if_true: files('virtio-rng.c'))
specific_virtio_ss.add(when: 'CONFIG_VIRTIO_IOMMU', if_true: files('virtio-iommu.c'))
specific_virtio_ss.add(when: 'CONFIG_VIRTIO_MEM', if_true: files('virtio-mem.c'))
specific_virtio_ss.add(when: 'CONFIG_VHOST_USER_I2C', if_true: files('vhost-user-i2c.c'))
specific_virtio_ss.add(when: 'CONFIG_VHOST_USER_RNG', if_true: files('vhost-user-rng.c'))
specific_virtio_ss.add(when: 'CONFIG_VHOST_USER_GPIO', if_true: files('vhost-user-gpio.c'))
specific_virtio_ss.add(when: ['CONFIG_VIRTIO_PCI', 'CONFIG_VHOST_USER_GPIO'], if_true: files('vhost-user-gpio-pci.c'))
specific_virtio_ss.add(when: 'CONFIG_VHOST_VDPA_DEV', if_true: files('vdpa-dev.c'))

virtio_pci_ss = ss.source_set()
virtio_pci_ss.add(when: 'CONFIG_VHOST_VSOCK', if_true: files('vhost-vsock-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VHOST_USER_VSOCK', if_true: files('vhost-user-vsock-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VHOST_USER_BLK', if_true: files('vhost-user-blk-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VHOST_USER_I2C', if_true: files('vhost-user-i2c-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VHOST_USER_INPUT', if_true: files('vhost-user-input-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VHOST_USER_RNG', if_true: files('vhost-user-rng-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VHOST_USER_SCSI', if_true: files('vhost-user-scsi-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VHOST_SCSI', if_true: files('vhost-scsi-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VHOST_USER_FS', if_true: files('vhost-user-fs-pci.c'))

virtio_pci_ss.add(when: 'CONFIG_VIRTIO_CRYPTO', if_true: files('virtio-crypto-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VIRTIO_INPUT_HOST', if_true: files('virtio-input-host-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VIRTIO_INPUT', if_true: files('virtio-input-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VIRTIO_RNG', if_true: files('virtio-rng-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VIRTIO_BALLOON', if_true: files('virtio-balloon-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VIRTIO_9P', if_true: files('virtio-9p-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VIRTIO_SCSI', if_true: files('virtio-scsi-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VIRTIO_BLK', if_true: files('virtio-blk-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VIRTIO_NET', if_true: files('virtio-net-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VIRTIO_SERIAL', if_true: files('virtio-serial-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VIRTIO_PMEM', if_true: files('virtio-pmem-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VIRTIO_IOMMU', if_true: files('virtio-iommu-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VIRTIO_MEM', if_true: files('virtio-mem-pci.c'))
virtio_pci_ss.add(when: 'CONFIG_VHOST_VDPA_DEV', if_true: files('vdpa-dev-pci.c'))

specific_virtio_ss.add_all(when: 'CONFIG_VIRTIO_PCI', if_true: virtio_pci_ss)

softmmu_ss.add_all(when: 'CONFIG_VIRTIO', if_true: softmmu_virtio_ss)
softmmu_ss.add(when: 'CONFIG_VIRTIO', if_false: files('vhost-stub.c'))
softmmu_ss.add(when: 'CONFIG_VIRTIO', if_false: files('virtio-stub.c'))
softmmu_ss.add(when: 'CONFIG_ALL', if_true: files('vhost-stub.c'))
softmmu_ss.add(when: 'CONFIG_ALL', if_true: files('virtio-stub.c'))
softmmu_ss.add(files('virtio-hmp-cmds.c'))

specific_ss.add_all(when: 'CONFIG_VIRTIO', if_true: specific_virtio_ss)
