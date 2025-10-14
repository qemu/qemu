scsi_ss = ss.source_set()
specific_scsi_ss = ss.source_set()
virtio_scsi_ss = ss.source_set()
specific_virtio_scsi_ss = ss.source_set()

scsi_ss.add(files(
  'emulation.c',
  'scsi-bus.c',
  'scsi-disk.c',
  'scsi-generic.c',
))
scsi_ss.add(when: 'CONFIG_ESP', if_true: files('esp.c'))
scsi_ss.add(when: 'CONFIG_ESP_PCI', if_true: files('esp-pci.c'))
scsi_ss.add(when: 'CONFIG_LSI_SCSI_PCI', if_true: files('lsi53c895a.c'))
scsi_ss.add(when: 'CONFIG_NCR710_SCSI', if_true: files('ncr53c710.c', 'lasi_ncr710.c'))
scsi_ss.add(when: 'CONFIG_MEGASAS_SCSI_PCI', if_true: files('megasas.c'))
scsi_ss.add(when: 'CONFIG_MPTSAS_SCSI_PCI', if_true: files('mptsas.c', 'mptconfig.c', 'mptendian.c'))
scsi_ss.add(when: 'CONFIG_VMW_PVSCSI_SCSI_PCI', if_true: files('vmw_pvscsi.c'))

virtio_scsi_ss.add(files('virtio-scsi-dataplane.c'))
virtio_scsi_ss.add(when: 'CONFIG_VHOST_SCSI', if_true: files('vhost-scsi.c'))
virtio_scsi_ss.add(when: 'CONFIG_VHOST_USER_SCSI', if_true: files('vhost-user-scsi.c'))

specific_virtio_scsi_ss.add(files('virtio-scsi.c'))
specific_virtio_scsi_ss.add(when: 'CONFIG_VHOST_SCSI_COMMON', if_true: files('vhost-scsi-common.c'))

specific_scsi_ss.add_all(when: 'CONFIG_VIRTIO_SCSI', if_true: specific_virtio_scsi_ss)
scsi_ss.add_all(when: 'CONFIG_VIRTIO_SCSI', if_true: virtio_scsi_ss)

specific_scsi_ss.add(when: 'CONFIG_SPAPR_VSCSI', if_true: files('spapr_vscsi.c'))

system_ss.add_all(when: 'CONFIG_SCSI', if_true: scsi_ss)
specific_ss.add_all(when: 'CONFIG_SCSI', if_true: specific_scsi_ss)
