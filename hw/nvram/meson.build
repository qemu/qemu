# QOM interfaces must be available anytime QOM is used.
qom_ss.add(files('fw_cfg-interface.c'))

softmmu_ss.add(files('fw_cfg.c'))
softmmu_ss.add(when: 'CONFIG_CHRP_NVRAM', if_true: files('chrp_nvram.c'))
softmmu_ss.add(when: 'CONFIG_DS1225Y', if_true: files('ds1225y.c'))
softmmu_ss.add(when: 'CONFIG_NMC93XX_EEPROM', if_true: files('eeprom93xx.c'))
softmmu_ss.add(when: 'CONFIG_AT24C', if_true: files('eeprom_at24c.c'))
softmmu_ss.add(when: 'CONFIG_MAC_NVRAM', if_true: files('mac_nvram.c'))
softmmu_ss.add(when: 'CONFIG_NPCM7XX', if_true: files('npcm7xx_otp.c'))
softmmu_ss.add(when: 'CONFIG_NRF51_SOC', if_true: files('nrf51_nvm.c'))

specific_ss.add(when: 'CONFIG_PSERIES', if_true: files('spapr_nvram.c'))
