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
  'block-active.c',
  'channel.c',
  'channel-block.c',
  'cpr.c',
  'cpr-transfer.c',
  'cpu-throttle.c',
  'dirtyrate.c',
  'exec.c',
  'fd.c',
  'file.c',
  'global_state.c',
  'migration-hmp-cmds.c',
  'migration.c',
  'multifd.c',
  'multifd-device-state.c',
  'multifd-nocomp.c',
  'multifd-zlib.c',
  'multifd-zero-page.c',
  'options.c',
  'postcopy-ram.c',
  'savevm.c',
  'socket.c',
  'tls.c',
  'threadinfo.c',
), gnutls, zlib)

if get_option('replication').allowed()
  system_ss.add(files('colo-failover.c', 'colo.c'))
else
  system_ss.add(files('colo-stubs.c'))
endif

system_ss.add(when: rdma, if_true: files('rdma.c'))
system_ss.add(when: zstd, if_true: files('multifd-zstd.c'))
system_ss.add(when: qpl, if_true: files('multifd-qpl.c'))
system_ss.add(when: uadk, if_true: files('multifd-uadk.c'))
system_ss.add(when: qatzip, if_true: files('multifd-qatzip.c'))

specific_ss.add(when: 'CONFIG_SYSTEM_ONLY',
                if_true: files('ram.c',
                               'target.c'))
