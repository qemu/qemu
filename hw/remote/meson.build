remote_ss = ss.source_set()

remote_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('machine.c'))
remote_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('mpqemu-link.c'))

softmmu_ss.add_all(when: 'CONFIG_MULTIPROCESS', if_true: remote_ss)
