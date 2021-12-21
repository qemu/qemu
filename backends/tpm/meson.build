if have_tpm
  softmmu_ss.add(files('tpm_backend.c'))
  softmmu_ss.add(files('tpm_util.c'))
  softmmu_ss.add(when: 'CONFIG_TPM_PASSTHROUGH', if_true: files('tpm_passthrough.c'))
  softmmu_ss.add(when: 'CONFIG_TPM_EMULATOR', if_true: files('tpm_emulator.c'))
endif
