softmmu_ss.add(when: 'CONFIG_DP8393X', if_true: files('dp8393x.c'))
softmmu_ss.add(when: 'CONFIG_XEN', if_true: files('xen_nic.c'))
softmmu_ss.add(when: 'CONFIG_NE2000_COMMON', if_true: files('ne2000.c'))

# PCI network cards
softmmu_ss.add(when: 'CONFIG_NE2000_PCI', if_true: files('ne2000-pci.c'))
softmmu_ss.add(when: 'CONFIG_EEPRO100_PCI', if_true: files('eepro100.c'))
softmmu_ss.add(when: 'CONFIG_PCNET_PCI', if_true: files('pcnet-pci.c'))
softmmu_ss.add(when: 'CONFIG_PCNET_COMMON', if_true: files('pcnet.c'))
softmmu_ss.add(when: 'CONFIG_E1000_PCI', if_true: files('e1000.c', 'e1000x_common.c'))
softmmu_ss.add(when: 'CONFIG_E1000E_PCI_EXPRESS', if_true: files('net_tx_pkt.c', 'net_rx_pkt.c'))
softmmu_ss.add(when: 'CONFIG_E1000E_PCI_EXPRESS', if_true: files('e1000e.c', 'e1000e_core.c', 'e1000x_common.c'))
softmmu_ss.add(when: 'CONFIG_RTL8139_PCI', if_true: files('rtl8139.c'))
softmmu_ss.add(when: 'CONFIG_TULIP', if_true: files('tulip.c'))
softmmu_ss.add(when: 'CONFIG_VMXNET3_PCI', if_true: files('net_tx_pkt.c', 'net_rx_pkt.c'))
softmmu_ss.add(when: 'CONFIG_VMXNET3_PCI', if_true: files('vmxnet3.c'))

softmmu_ss.add(when: 'CONFIG_SMC91C111', if_true: files('smc91c111.c'))
softmmu_ss.add(when: 'CONFIG_LAN9118', if_true: files('lan9118.c'))
softmmu_ss.add(when: 'CONFIG_NE2000_ISA', if_true: files('ne2000-isa.c'))
softmmu_ss.add(when: 'CONFIG_OPENCORES_ETH', if_true: files('opencores_eth.c'))
softmmu_ss.add(when: 'CONFIG_XGMAC', if_true: files('xgmac.c'))
softmmu_ss.add(when: 'CONFIG_MIPSNET', if_true: files('mipsnet.c'))
softmmu_ss.add(when: 'CONFIG_XILINX_AXI', if_true: files('xilinx_axienet.c'))
softmmu_ss.add(when: 'CONFIG_ALLWINNER_EMAC', if_true: files('allwinner_emac.c'))
softmmu_ss.add(when: 'CONFIG_ALLWINNER_SUN8I_EMAC', if_true: files('allwinner-sun8i-emac.c'))
softmmu_ss.add(when: 'CONFIG_IMX_FEC', if_true: files('imx_fec.c'))
softmmu_ss.add(when: 'CONFIG_MSF2', if_true: files('msf2-emac.c'))
softmmu_ss.add(when: 'CONFIG_MARVELL_88W8618', if_true: files('mv88w8618_eth.c'))

softmmu_ss.add(when: 'CONFIG_CADENCE', if_true: files('cadence_gem.c'))
softmmu_ss.add(when: 'CONFIG_STELLARIS_ENET', if_true: files('stellaris_enet.c'))
softmmu_ss.add(when: 'CONFIG_LANCE', if_true: files('lance.c'))
softmmu_ss.add(when: 'CONFIG_LASI_I82596', if_true: files('lasi_i82596.c'))
softmmu_ss.add(when: 'CONFIG_I82596_COMMON', if_true: files('i82596.c'))
softmmu_ss.add(when: 'CONFIG_SUNHME', if_true: files('sunhme.c'))
softmmu_ss.add(when: 'CONFIG_FTGMAC100', if_true: files('ftgmac100.c'))
softmmu_ss.add(when: 'CONFIG_SUNGEM', if_true: files('sungem.c'))
softmmu_ss.add(when: 'CONFIG_NPCM7XX', if_true: files('npcm7xx_emc.c'))

softmmu_ss.add(when: 'CONFIG_ETRAXFS', if_true: files('etraxfs_eth.c'))
softmmu_ss.add(when: 'CONFIG_COLDFIRE', if_true: files('mcf_fec.c'))
specific_ss.add(when: 'CONFIG_PSERIES', if_true: files('spapr_llan.c'))
specific_ss.add(when: 'CONFIG_XILINX_ETHLITE', if_true: files('xilinx_ethlite.c'))

softmmu_ss.add(when: 'CONFIG_VIRTIO_NET', if_true: files('net_rx_pkt.c'))
specific_ss.add(when: 'CONFIG_VIRTIO_NET', if_true: files('virtio-net.c'))

softmmu_ss.add(when: ['CONFIG_VIRTIO_NET', 'CONFIG_VHOST_NET'], if_true: files('vhost_net.c'), if_false: files('vhost_net-stub.c'))
softmmu_ss.add(when: 'CONFIG_ALL', if_true: files('vhost_net-stub.c'))

softmmu_ss.add(when: 'CONFIG_ETSEC', if_true: files(
  'fsl_etsec/etsec.c',
  'fsl_etsec/miim.c',
  'fsl_etsec/registers.c',
  'fsl_etsec/rings.c',
))

softmmu_ss.add(when: 'CONFIG_ROCKER', if_true: files(
  'rocker/rocker.c',
  'rocker/rocker_desc.c',
  'rocker/rocker_fp.c',
  'rocker/rocker_of_dpa.c',
  'rocker/rocker_world.c',
), if_false: files('rocker/qmp-norocker.c'))
softmmu_ss.add(when: 'CONFIG_ALL', if_true: files('rocker/qmp-norocker.c'))

subdir('can')
