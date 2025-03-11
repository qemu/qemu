vfio_ss = ss.source_set()
vfio_ss.add(files(
  'common.c',
  'container.c',
))
vfio_ss.add(when: 'CONFIG_PSERIES', if_true: files('spapr.c'))
vfio_ss.add(when: 'CONFIG_VFIO_PCI', if_true: files(
  'pci-quirks.c',
  'pci.c',
))
vfio_ss.add(when: 'CONFIG_VFIO_CCW', if_true: files('ccw.c'))
vfio_ss.add(when: 'CONFIG_VFIO_PLATFORM', if_true: files('platform.c'))
vfio_ss.add(when: 'CONFIG_VFIO_AP', if_true: files('ap.c'))
vfio_ss.add(when: 'CONFIG_VFIO_IGD', if_true: files('igd.c'))

specific_ss.add_all(when: 'CONFIG_VFIO', if_true: vfio_ss)

system_ss.add(when: 'CONFIG_VFIO_XGMAC', if_true: files('calxeda-xgmac.c'))
system_ss.add(when: 'CONFIG_VFIO_AMD_XGBE', if_true: files('amd-xgbe.c'))
system_ss.add(when: 'CONFIG_VFIO', if_true: files(
  'helpers.c',
  'container-base.c',
  'migration.c',
  'migration-multifd.c',
  'cpr.c',
))
system_ss.add(when: ['CONFIG_VFIO', 'CONFIG_IOMMUFD'], if_true: files(
  'iommufd.c',
))
system_ss.add(when: 'CONFIG_VFIO_PCI', if_true: files(
  'display.c',
))
