ppc_ss = ss.source_set()
ppc_ss.add(files(
  'ppc.c',
  'ppc_booke.c',
))
ppc_ss.add(when: 'CONFIG_FDT_PPC', if_true: [files(
  'fdt.c',
), fdt])
ppc_ss.add(when: 'CONFIG_FW_CFG_PPC', if_true: files('fw_cfg.c'))

# IBM pSeries (sPAPR)
ppc_ss.add(when: 'CONFIG_PSERIES', if_true: files(
  'spapr.c',
  'spapr_caps.c',
  'spapr_vio.c',
  'spapr_events.c',
  'spapr_hcall.c',
  'spapr_nested.c',
  'spapr_iommu.c',
  'spapr_rtas.c',
  'spapr_pci.c',
  'spapr_rtc.c',
  'spapr_drc.c',
  'spapr_cpu_core.c',
  'spapr_ovec.c',
  'spapr_irq.c',
  'spapr_tpm_proxy.c',
  'spapr_nvdimm.c',
  'spapr_rtas_ddw.c',
  'spapr_numa.c',
  'pef.c',
))
ppc_ss.add(when: ['CONFIG_PSERIES', 'CONFIG_TCG'], if_true: files(
  'spapr_softmmu.c',
))
ppc_ss.add(when: 'CONFIG_SPAPR_RNG', if_true: files('spapr_rng.c'))
ppc_ss.add(when: ['CONFIG_PSERIES', 'CONFIG_LINUX'], if_true: files(
  'spapr_pci_vfio.c',
  'spapr_pci_nvlink2.c'
))

# IBM PowerNV
ppc_ss.add(when: 'CONFIG_POWERNV', if_true: files(
  'pnv.c',
  'pnv_xscom.c',
  'pnv_core.c',
  'pnv_lpc.c',
  'pnv_psi.c',
  'pnv_occ.c',
  'pnv_sbe.c',
  'pnv_bmc.c',
  'pnv_homer.c',
  'pnv_pnor.c',
))
# PowerPC 4xx boards
ppc_ss.add(when: 'CONFIG_PPC405', if_true: files(
  'ppc405_boards.c',
  'ppc405_uc.c'))
ppc_ss.add(when: 'CONFIG_PPC440', if_true: files(
  'ppc440_bamboo.c',
  'ppc440_pcix.c', 'ppc440_uc.c'))
ppc_ss.add(when: 'CONFIG_PPC4XX', if_true: files(
  'ppc4xx_devs.c',
  'ppc4xx_pci.c',
  'ppc4xx_sdram.c'))
ppc_ss.add(when: 'CONFIG_SAM460EX', if_true: files('sam460ex.c'))
# PReP
ppc_ss.add(when: 'CONFIG_PREP', if_true: files('prep.c'))
ppc_ss.add(when: 'CONFIG_PREP', if_true: files('prep_systemio.c'))
ppc_ss.add(when: 'CONFIG_RS6000_MC', if_true: files('rs6000_mc.c'))
# OldWorld PowerMac
ppc_ss.add(when: 'CONFIG_MAC_OLDWORLD', if_true: files('mac_oldworld.c'))
# NewWorld PowerMac
ppc_ss.add(when: 'CONFIG_MAC_NEWWORLD', if_true: files('mac_newworld.c'))
# e500
ppc_ss.add(when: 'CONFIG_E500PLAT', if_true: files('e500plat.c'))
ppc_ss.add(when: 'CONFIG_MPC8544DS', if_true: files('mpc8544ds.c'))
ppc_ss.add(when: 'CONFIG_E500', if_true: files(
  'e500.c',
  'mpc8544_guts.c',
  'ppce500_spin.c'
))
# PowerPC 440 Xilinx ML507 reference board.
ppc_ss.add(when: 'CONFIG_VIRTEX', if_true: files('virtex_ml507.c'))
# Pegasos2
ppc_ss.add(when: 'CONFIG_PEGASOS2', if_true: files('pegasos2.c'))

ppc_ss.add(when: 'CONFIG_VOF', if_true: files('vof.c'))
ppc_ss.add(when: ['CONFIG_VOF', 'CONFIG_PSERIES'], if_true: files('spapr_vof.c'))

hw_arch += {'ppc': ppc_ss}
