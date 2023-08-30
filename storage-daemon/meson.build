qsd_ss = ss.source_set()
qsd_ss.add(files('qemu-storage-daemon.c'))
qsd_ss.add(blockdev, chardev, qmp, qom, qemuutil, gnutls)

subdir('qapi')

if have_tools
  qsd_ss = qsd_ss.apply({})
  qsd = executable('qemu-storage-daemon',
                   qsd_ss.sources(),
                   dependencies: qsd_ss.dependencies(),
                   install: true)
endif
