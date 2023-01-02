softmmu_ss.add(files(
  'block.c',
  'cdrom.c',
  'hd-geometry.c'
))
softmmu_ss.add(when: 'CONFIG_ECC', if_true: files('ecc.c'))
softmmu_ss.add(when: 'CONFIG_FDC', if_true: files('fdc.c'))
softmmu_ss.add(when: 'CONFIG_FDC_ISA', if_true: files('fdc-isa.c'))
softmmu_ss.add(when: 'CONFIG_FDC_SYSBUS', if_true: files('fdc-sysbus.c'))
softmmu_ss.add(when: 'CONFIG_NAND', if_true: files('nand.c'))
softmmu_ss.add(when: 'CONFIG_ONENAND', if_true: files('onenand.c'))
softmmu_ss.add(when: 'CONFIG_PFLASH_CFI01', if_true: files('pflash_cfi01.c'))
softmmu_ss.add(when: 'CONFIG_PFLASH_CFI02', if_true: files('pflash_cfi02.c'))
softmmu_ss.add(when: 'CONFIG_SSI_M25P80', if_true: files('m25p80.c'))
softmmu_ss.add(when: 'CONFIG_SSI_M25P80', if_true: files('m25p80_sfdp.c'))
softmmu_ss.add(when: 'CONFIG_SWIM', if_true: files('swim.c'))
softmmu_ss.add(when: 'CONFIG_XEN_BUS', if_true: files('xen-block.c'))
softmmu_ss.add(when: 'CONFIG_TC58128', if_true: files('tc58128.c'))

specific_ss.add(when: 'CONFIG_VIRTIO_BLK', if_true: files('virtio-blk.c', 'virtio-blk-common.c'))
specific_ss.add(when: 'CONFIG_VHOST_USER_BLK', if_true: files('vhost-user-blk.c', 'virtio-blk-common.c'))

subdir('dataplane')
