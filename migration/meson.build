# Files needed by unit tests
migration_files = files(
  'page_cache.c',
  'xbzrle.c',
  'vmstate-types.c',
  'vmstate.c',
  'qemu-file-channel.c',
  'qemu-file.c',
  'yank_functions.c',
)
softmmu_ss.add(migration_files)

softmmu_ss.add(files(
  'block-dirty-bitmap.c',
  'channel.c',
  'colo-failover.c',
  'colo.c',
  'exec.c',
  'fd.c',
  'global_state.c',
  'migration.c',
  'multifd.c',
  'multifd-zlib.c',
  'postcopy-ram.c',
  'savevm.c',
  'socket.c',
  'tls.c',
), gnutls)

softmmu_ss.add(when: ['CONFIG_RDMA', rdma], if_true: files('rdma.c'))
softmmu_ss.add(when: 'CONFIG_LIVE_BLOCK_MIGRATION', if_true: files('block.c'))
softmmu_ss.add(when: zstd, if_true: files('multifd-zstd.c'))

specific_ss.add(when: 'CONFIG_SOFTMMU',
                if_true: files('dirtyrate.c', 'ram.c', 'target.c'))
