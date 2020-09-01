nubus_ss = ss.source_set()
nubus_ss.add(files('nubus-device.c'))
nubus_ss.add(files('nubus-bus.c'))
nubus_ss.add(files('nubus-bridge.c'))
nubus_ss.add(when: 'CONFIG_Q800', if_true: files('mac-nubus-bridge.c'))

softmmu_ss.add_all(when: 'CONFIG_NUBUS', if_true: nubus_ss)
