softmmu_ss.add(when: 'CONFIG_TPM_TIS', if_true: files('tpm_tis_common.c'))
softmmu_ss.add(when: 'CONFIG_TPM_TIS_ISA', if_true: files('tpm_tis_isa.c'))
softmmu_ss.add(when: 'CONFIG_TPM_TIS_SYSBUS', if_true: files('tpm_tis_sysbus.c'))
softmmu_ss.add(when: 'CONFIG_TPM_CRB', if_true: files('tpm_crb.c'))
softmmu_ss.add(when: 'CONFIG_TPM_TIS', if_true: files('tpm_ppi.c'))
softmmu_ss.add(when: 'CONFIG_TPM_CRB', if_true: files('tpm_ppi.c'))

specific_ss.add(when: 'CONFIG_TPM_SPAPR', if_true: files('tpm_spapr.c'))
