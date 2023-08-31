remote_ss = ss.source_set()

remote_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('machine.c'))
remote_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('mpqemu-link.c'))
remote_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('message.c'))
remote_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('remote-obj.c'))
remote_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('proxy.c'))
remote_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('iohub.c'))
remote_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('iommu.c'))

remote_ss.add(when: 'CONFIG_VFIO_USER_SERVER', if_true: libvfio_user_dep)
remote_ss.add(when: 'CONFIG_VFIO_USER_SERVER', if_true: files('vfio-user-obj.c'),
              if_false: files('vfio-user-obj-stub.c'))
remote_ss.add(when: 'CONFIG_ALL', if_true: files('vfio-user-obj-stub.c'))

specific_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('memory.c'))
specific_ss.add(when: 'CONFIG_MULTIPROCESS', if_true: files('proxy-memory-listener.c'))

system_ss.add_all(when: 'CONFIG_MULTIPROCESS', if_true: remote_ss)
