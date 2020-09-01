s390x_ss = ss.source_set()
s390x_ss.add(files(
  'ap-bridge.c',
  'ap-device.c',
  'ccw-device.c',
  'css-bridge.c',
  'css.c',
  'event-facility.c',
  'ipl.c',
  's390-ccw.c',
  's390-pci-bus.c',
  's390-pci-inst.c',
  's390-skeys.c',
  's390-stattrib.c',
  's390-virtio-hcall.c',
  'sclp.c',
  'sclpcpu.c',
  'sclpquiesce.c',
  'tod-qemu.c',
  'tod.c',
))
s390x_ss.add(when: 'CONFIG_KVM', if_true: files(
  'tod-kvm.c',
  's390-skeys-kvm.c',
  's390-stattrib-kvm.c',
  'pv.c',
))
s390x_ss.add(when: 'CONFIG_S390_CCW_VIRTIO', if_true: files('s390-virtio-ccw.c'))
s390x_ss.add(when: 'CONFIG_TERMINAL3270', if_true: files('3270-ccw.c'))

virtio_ss = ss.source_set()
virtio_ss.add(files('virtio-ccw.c'))
virtio_ss.add(when: 'CONFIG_VIRTIO_BALLOON', if_true: files('virtio-ccw-balloon.c'))
virtio_ss.add(when: 'CONFIG_VIRTIO_BLK', if_true: files('virtio-ccw-blk.c'))
virtio_ss.add(when: 'CONFIG_VIRTIO_CRYPTO', if_true: files('virtio-ccw-crypto.c'))
virtio_ss.add(when: 'CONFIG_VIRTIO_GPU', if_true: files('virtio-ccw-gpu.c'))
virtio_ss.add(when: 'CONFIG_VIRTIO_INPUT', if_true: files('virtio-ccw-input.c'))
virtio_ss.add(when: 'CONFIG_VIRTIO_NET', if_true: files('virtio-ccw-net.c'))
virtio_ss.add(when: 'CONFIG_VIRTIO_RNG', if_true: files('virtio-ccw-rng.c'))
virtio_ss.add(when: 'CONFIG_VIRTIO_SCSI', if_true: files('virtio-ccw-scsi.c'))
virtio_ss.add(when: 'CONFIG_VIRTIO_SERIAL', if_true: files('virtio-ccw-serial.c'))
virtio_ss.add(when: ['CONFIG_VIRTIO_9P', 'CONFIG_VIRTFS'], if_true: files('virtio-ccw-blk.c'))
virtio_ss.add(when: 'CONFIG_VHOST_VSOCK', if_true: files('vhost-vsock-ccw.c'))
s390x_ss.add_all(when: 'CONFIG_VIRTIO_CCW', if_true: virtio_ss)

hw_arch += {'s390x': s390x_ss}
