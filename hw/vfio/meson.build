# SPDX-License-Identifier: GPL-2.0-or-later

vfio_ss = ss.source_set()
vfio_ss.add(files(
  'listener.c',
  'container.c',
  'container-legacy.c',
  'helpers.c',
))
vfio_ss.add(when: 'CONFIG_PSERIES', if_true: files('spapr.c'))
vfio_ss.add(when: 'CONFIG_VFIO_PCI', if_true: files(
  'pci-quirks.c',
  'pci.c',
))
vfio_ss.add(when: 'CONFIG_VFIO_CCW', if_true: files('ccw.c'))
vfio_ss.add(when: 'CONFIG_VFIO_AP', if_true: files('ap.c'))
vfio_ss.add(when: 'CONFIG_VFIO_IGD', if_true: files('igd.c'))

specific_ss.add_all(when: 'CONFIG_VFIO', if_true: vfio_ss)

system_ss.add(when: 'CONFIG_VFIO', if_true: files(
  'cpr.c',
  'cpr-legacy.c',
  'device.c',
  'migration.c',
  'migration-multifd.c',
  'region.c',
))
system_ss.add(when: ['CONFIG_VFIO', 'CONFIG_IOMMUFD'], if_true: files(
  'iommufd.c',
  'cpr-iommufd.c',
))
system_ss.add(when: 'CONFIG_IOMMUFD', if_false: files('iommufd-stubs.c'))
system_ss.add(when: 'CONFIG_VFIO_PCI', if_true: files(
  'display.c',
))
