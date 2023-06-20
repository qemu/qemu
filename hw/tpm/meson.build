system_ss.add(when: 'CONFIG_TPM_TIS', if_true: files('tpm_tis_common.c'))
system_ss.add(when: 'CONFIG_TPM_TIS_ISA', if_true: files('tpm_tis_isa.c'))
system_ss.add(when: 'CONFIG_TPM_TIS_SYSBUS', if_true: files('tpm_tis_sysbus.c'))
system_ss.add(when: 'CONFIG_TPM_TIS_I2C', if_true: files('tpm_tis_i2c.c'))
system_ss.add(when: 'CONFIG_TPM_CRB', if_true: files('tpm_crb.c'))
system_ss.add(when: 'CONFIG_TPM_TIS', if_true: files('tpm_ppi.c'))
system_ss.add(when: 'CONFIG_TPM_CRB', if_true: files('tpm_ppi.c'))

specific_ss.add(when: 'CONFIG_TPM_SPAPR', if_true: files('tpm_spapr.c'))
