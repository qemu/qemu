# SPDX-License-Identifier: GPL-2.0-or-later

vfio_user_ss = ss.source_set()
vfio_user_ss.add(files(
  'container.c',
  'device.c',
  'pci.c',
  'proxy.c',
))

system_ss.add_all(when: 'CONFIG_VFIO_USER', if_true: vfio_user_ss)
