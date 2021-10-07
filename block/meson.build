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
  'snapshot.c',
  'throttle-groups.c',
  'throttle.c',
  'vhdx-endian.c',
  'vhdx-log.c',
  'vhdx.c',
  'vmdk.c',
  'vpc.c',
  'write-threshold.c',
), zstd, zlib, gnutls)

softmmu_ss.add(when: 'CONFIG_TCG', if_true: files('blkreplay.c'))

block_ss.add(when: 'CONFIG_QCOW1', if_true: files('qcow.c'))
block_ss.add(when: 'CONFIG_VDI', if_true: files('vdi.c'))
block_ss.add(when: 'CONFIG_CLOOP', if_true: files('cloop.c'))
block_ss.add(when: 'CONFIG_BOCHS', if_true: files('bochs.c'))
block_ss.add(when: 'CONFIG_VVFAT', if_true: files('vvfat.c'))
block_ss.add(when: 'CONFIG_DMG', if_true: files('dmg.c'))
block_ss.add(when: 'CONFIG_QED', if_true: files(
  'qed-check.c',
  'qed-cluster.c',
  'qed-l2-cache.c',
  'qed-table.c',
  'qed.c',
))
block_ss.add(when: [libxml2, 'CONFIG_PARALLELS'],
             if_true: files('parallels.c', 'parallels-ext.c'))
block_ss.add(when: 'CONFIG_WIN32', if_true: files('file-win32.c', 'win32-aio.c'))
block_ss.add(when: 'CONFIG_POSIX', if_true: [files('file-posix.c'), coref, iokit])
block_ss.add(when: libiscsi, if_true: files('iscsi-opts.c'))
block_ss.add(when: 'CONFIG_LINUX', if_true: files('nvme.c'))
block_ss.add(when: 'CONFIG_REPLICATION', if_true: files('replication.c'))
block_ss.add(when: libaio, if_true: files('linux-aio.c'))
block_ss.add(when: linux_io_uring, if_true: files('io_uring.c'))

block_modules = {}

modsrc = []
foreach m : [
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
if 'CONFIG_DMG' in config_host
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
                            input: files('../include/block/block.h',
                                         'coroutines.h'),
                            command: [wrapper_py, '@OUTPUT@', '@INPUT@'])
block_ss.add(block_gen_c)

block_ss.add(files('stream.c'))

softmmu_ss.add(files('qapi-sysemu.c'))

subdir('export')
subdir('monitor')

modules += {'block': block_modules}
