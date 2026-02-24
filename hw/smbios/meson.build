smbios_ss = ss.source_set()
smbios_ss.add(files('smbios.c'))
smbios_ss.add(when: 'CONFIG_IPMI',
              if_true: files('smbios_type_38.c'))
stub_ss.add(files('smbios_type_38-stub.c'))

smbios_ss.add(when: 'CONFIG_SMBIOS_LEGACY',
              if_true: files('smbios_legacy.c'))
stub_ss.add(files('smbios_legacy_stub.c'))

system_ss.add_all(when: 'CONFIG_SMBIOS', if_true: smbios_ss)
stub_ss.add(files('smbios-stub.c'))
