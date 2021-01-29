remote_ss = ss.source_set()

remote_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('machine.c'))
remote_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('mpqemu-link.c'))
remote_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('message.c'))
remote_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('remote-obj.c'))
remote_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('proxy.c'))
remote_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('iohub.c'))

specific_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('memory.c'))
specific_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('proxy-memory-listener.c'))

softmmu_ss.add_all(when: 'CONFIG_MULTIPROCESS', if_true: remote_ss)
