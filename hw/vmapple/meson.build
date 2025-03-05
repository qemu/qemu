# SPDX-License-Identifier: GPL-2.0-or-later

system_ss.add(when: 'CONFIG_VMAPPLE_AES',  if_true: files('aes.c'))
system_ss.add(when: 'CONFIG_VMAPPLE_BDIF', if_true: files('bdif.c'))
system_ss.add(when: 'CONFIG_VMAPPLE_CFG',  if_true: files('cfg.c'))
system_ss.add(when: 'CONFIG_VMAPPLE_VIRTIO_BLK',  if_true: files('virtio-blk.c'))
specific_ss.add(when: 'CONFIG_VMAPPLE',     if_true: files('vmapple.c'))
