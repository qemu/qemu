block_ss.add(genh)
block_ss.add(files(
  'accounting.c',
  'aio_task.c',
  'amend.c',
  'backup.c',
  'copy-before-write.c',
  'blkdebug.c',
  'blklogwrites.c',
  'blkverify.c',
  'block-backend.c',
  'block-copy.c',
  'graph-lock.c',
  'commit.c',
  'copy-on-read.c',
  'preallocate.c',
  'progress_meter.c',
  'create.c',
  'crypto.c',
  'dirty-bitmap.c',
  'filter-compress.c',
  'io.c',
  'mirror.c',
  'nbd.c',
  'null.c',
  'plug.c',
  'qapi.c',
  'qcow2-bitmap.c',
  'qcow2-cache.c',
  'qcow2-cluster.c',
  'qcow2-refcount.c',
  'qcow2-snapshot.c',
  'qcow2-threads.c',
  'qcow2.c',
  'quorum.c',
  'raw-format.c',
  'reqlist.c',
  'snapshot.c',
  'snapshot-access.c',
  'throttle-groups.c',
  'throttle.c',
  'write-threshold.c',
), zstd, zlib, gnutls)

system_ss.add(when: 'CONFIG_TCG', if_true: files('blkreplay.c'))
system_ss.add(files('block-ram-registrar.c'))

if get_option('qcow1').allowed()
  block_ss.add(files('qcow.c'))
endif
if get_option('vdi').allowed()
  block_ss.add(files('vdi.c'))
endif
if get_option('vhdx').allowed()
  block_ss.add(files(
    'vhdx-endian.c',
    'vhdx-log.c',
    'vhdx.c'
  ))
endif
if get_option('vmdk').allowed()
  block_ss.add(files('vmdk.c'))
endif
if get_option('vpc').allowed()
  block_ss.add(files('vpc.c'))
endif
if get_option('cloop').allowed()
  block_ss.add(files('cloop.c'))
endif
if get_option('bochs').allowed()
  block_ss.add(files('bochs.c'))
endif
if get_option('vvfat').allowed()
  block_ss.add(files('vvfat.c'))
endif
if get_option('dmg').allowed()
  block_ss.add(files('dmg.c'))
endif
if get_option('qed').allowed()
  block_ss.add(files(
    'qed-check.c',
    'qed-cluster.c',
    'qed-l2-cache.c',
    'qed-table.c',
    'qed.c',
  ))
endif
if get_option('parallels').allowed()
  block_ss.add(files('parallels.c', 'parallels-ext.c'))
endif

block_ss.add(when: 'CONFIG_WIN32', if_true: files('file-win32.c', 'win32-aio.c'))
block_ss.add(when: 'CONFIG_POSIX', if_true: [files('file-posix.c'), coref, iokit])
block_ss.add(when: libiscsi, if_true: files('iscsi-opts.c'))
block_ss.add(when: 'CONFIG_LINUX', if_true: files('nvme.c'))
if get_option('replication').allowed()
  block_ss.add(files('replication.c'))
endif
block_ss.add(when: libaio, if_true: files('linux-aio.c'))
block_ss.add(when: linux_io_uring, if_true: files('io_uring.c'))

block_modules = {}

modsrc = []
foreach m : [
  [blkio, 'blkio', files('blkio.c')],
  [curl, 'curl', files('curl.c')],
  [glusterfs, 'gluster', files('gluster.c')],
  [libiscsi, 'iscsi', [files('iscsi.c'), libm]],
  [libnfs, 'nfs', files('nfs.c')],
  [libssh, 'ssh', files('ssh.c')],
  [rbd, 'rbd', files('rbd.c')],
]
  if m[0].found()
    module_ss = ss.source_set()
    module_ss.add(when: m[0], if_true: m[2])
    if enable_modules
      modsrc += module_ss.all_sources()
    endif
    block_modules += {m[1] : module_ss}
  endif
endforeach

# those are not exactly regular block modules, so treat them apart
if get_option('dmg').allowed()
  foreach m : [
    [liblzfse, 'dmg-lzfse', liblzfse, 'dmg-lzfse.c'],
    [libbzip2, 'dmg-bz2', [glib, libbzip2], 'dmg-bz2.c']
  ]
    if m[0].found()
      module_ss = ss.source_set()
      module_ss.add(when: m[2], if_true: files(m[3]))
      block_modules += {m[1] : module_ss}
    endif
  endforeach
endif

module_block_py = find_program('../scripts/modules/module_block.py')
module_block_h = custom_target('module_block.h',
                               output: 'module_block.h',
                               input: modsrc,
                               command: [module_block_py, '@OUTPUT0@', modsrc])
block_ss.add(module_block_h)

wrapper_py = find_program('../scripts/block-coroutine-wrapper.py')
block_gen_c = custom_target('block-gen.c',
                            output: 'block-gen.c',
                            input: files(
                                      '../include/block/block-io.h',
                                      '../include/block/dirty-bitmap.h',
                                      '../include/block/block_int-io.h',
                                      '../include/block/block-global-state.h',
                                      '../include/sysemu/block-backend-global-state.h',
                                      '../include/sysemu/block-backend-io.h',
                                      'coroutines.h'
                                      ),
                            command: [wrapper_py, '@OUTPUT@', '@INPUT@'])
block_ss.add(block_gen_c)

block_ss.add(files('stream.c'))

system_ss.add(files('qapi-sysemu.c'))

subdir('export')
subdir('monitor')

modules += {'block': block_modules}
