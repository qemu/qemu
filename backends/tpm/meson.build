tpm_ss = ss.source_set()

tpm_ss.add(files('tpm_backend.c'))
tpm_ss.add(files('tpm_util.c'))
tpm_ss.add(when: 'CONFIG_TPM_PASSTHROUGH', if_true: files('tpm_passthrough.c'))
tpm_ss.add(when: 'CONFIG_TPM_EMULATOR', if_true: files('tpm_emulator.c'))

softmmu_ss.add_all(when: 'CONFIG_TPM', if_true: tpm_ss)
