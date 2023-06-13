# Files needed by unit tests
migration_files = files(
  'migration-stats.c',
  'page_cache.c',
  'xbzrle.c',
  'vmstate-types.c',
  'vmstate.c',
  'qemu-file.c',
  'yank_functions.c',
)

system_ss.add(files(
  'block-dirty-bitmap.c',
  'channel.c',
  'channel-block.c',
  'dirtyrate.c',
  'exec.c',
  'fd.c',
  'global_state.c',
  'migration-hmp-cmds.c',
  'migration.c',
  'multifd.c',
  'multifd-zlib.c',
  'ram-compress.c',
  'options.c',
  'postcopy-ram.c',
  'savevm.c',
  'socket.c',
  'tls.c',
  'threadinfo.c',
), gnutls)

if get_option('replication').allowed()
  system_ss.add(files('colo-failover.c', 'colo.c'))
endif

system_ss.add(when: rdma, if_true: files('rdma.c'))
if get_option('live_block_migration').allowed()
  system_ss.add(files('block.c'))
endif
system_ss.add(when: zstd, if_true: files('multifd-zstd.c'))

specific_ss.add(when: 'CONFIG_SYSTEM_ONLY',
                if_true: files('ram.c',
                               'target.c'))
