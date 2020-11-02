vfio_ss = ss.source_set()
vfio_ss.add(files(
  'common.c',
  'spapr.c',
  'migration.c',
))
vfio_ss.add(when: 'CONFIG_VFIO_PCI', if_true: files(
  'display.c',
  'pci-quirks.c',
  'pci.c',
))
vfio_ss.add(when: 'CONFIG_VFIO_CCW', if_true: files('ccw.c'))
vfio_ss.add(when: 'CONFIG_VFIO_PLATFORM', if_true: files('platform.c'))
vfio_ss.add(when: 'CONFIG_VFIO_XGMAC', if_true: files('calxeda-xgmac.c'))
vfio_ss.add(when: 'CONFIG_VFIO_AMD_XGBE', if_true: files('amd-xgbe.c'))
vfio_ss.add(when: 'CONFIG_VFIO_AP', if_true: files('ap.c'))
vfio_ss.add(when: 'CONFIG_VFIO_IGD', if_true: files('igd.c'))

specific_ss.add_all(when: 'CONFIG_VFIO', if_true: vfio_ss)
