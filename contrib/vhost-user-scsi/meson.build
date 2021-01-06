if libiscsi.found()
  executable('vhost-user-scsi', files('vhost-user-scsi.c'),
             dependencies: [qemuutil, libiscsi, vhost_user],
             build_by_default: targetos == 'linux',
             install: false)
endif
