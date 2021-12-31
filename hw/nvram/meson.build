if have_system or have_tools
  # QOM interfaces must be available anytime QOM is used.
  qom_ss.add(files('fw_cfg-interface.c'))
endif

softmmu_ss.add(files('fw_cfg.c'))
softmmu_ss.add(when: 'CONFIG_CHRP_NVRAM', if_true: files('chrp_nvram.c'))
softmmu_ss.add(when: 'CONFIG_DS1225Y', if_true: files('ds1225y.c'))
softmmu_ss.add(when: 'CONFIG_NMC93XX_EEPROM', if_true: files('eeprom93xx.c'))
softmmu_ss.add(when: 'CONFIG_AT24C', if_true: files('eeprom_at24c.c'))
softmmu_ss.add(when: 'CONFIG_MAC_NVRAM', if_true: files('mac_nvram.c'))
softmmu_ss.add(when: 'CONFIG_NPCM7XX', if_true: files('npcm7xx_otp.c'))
softmmu_ss.add(when: 'CONFIG_NRF51_SOC', if_true: files('nrf51_nvm.c'))
softmmu_ss.add(when: 'CONFIG_XLNX_EFUSE_CRC', if_true: files('xlnx-efuse-crc.c'))
softmmu_ss.add(when: 'CONFIG_XLNX_EFUSE', if_true: files('xlnx-efuse.c'))
softmmu_ss.add(when: 'CONFIG_XLNX_EFUSE_VERSAL', if_true: files(
                                                   'xlnx-versal-efuse-cache.c',
                                                   'xlnx-versal-efuse-ctrl.c'))
softmmu_ss.add(when: 'CONFIG_XLNX_EFUSE_ZYNQMP', if_true: files(
                                                   'xlnx-zynqmp-efuse.c'))
softmmu_ss.add(when: 'CONFIG_XLNX_BBRAM', if_true: files('xlnx-bbram.c'))

specific_ss.add(when: 'CONFIG_PSERIES', if_true: files('spapr_nvram.c'))
