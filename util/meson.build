util_ss.add(files('osdep.c', 'cutils.c', 'unicode.c', 'qemu-timer-common.c'))
util_ss.add(files('thread-context.c'), numa)
if not config_host_data.get('CONFIG_ATOMIC64')
  util_ss.add(files('atomic64.c'))
endif
util_ss.add(when: 'CONFIG_LINUX', if_true: files('async-teardown.c'))
util_ss.add(when: 'CONFIG_POSIX', if_true: files('aio-posix.c'))
util_ss.add(when: 'CONFIG_POSIX', if_true: files('fdmon-poll.c'))
if config_host_data.get('CONFIG_EPOLL_CREATE1')
  util_ss.add(files('fdmon-epoll.c'))
endif
util_ss.add(when: linux_io_uring, if_true: files('fdmon-io_uring.c'))
util_ss.add(when: 'CONFIG_POSIX', if_true: files('compatfd.c'))
util_ss.add(when: 'CONFIG_POSIX', if_true: files('event_notifier-posix.c'))
util_ss.add(when: 'CONFIG_POSIX', if_true: files('mmap-alloc.c'))
freebsd_dep = []
if targetos == 'freebsd'
  freebsd_dep = util
endif
util_ss.add(when: 'CONFIG_POSIX', if_true: [files('oslib-posix.c'), freebsd_dep])
util_ss.add(when: 'CONFIG_POSIX', if_true: files('qemu-thread-posix.c'))
util_ss.add(when: 'CONFIG_POSIX', if_true: files('memfd.c'))
util_ss.add(when: 'CONFIG_WIN32', if_true: files('aio-win32.c'))
util_ss.add(when: 'CONFIG_WIN32', if_true: files('event_notifier-win32.c'))
util_ss.add(when: 'CONFIG_WIN32', if_true: files('oslib-win32.c'))
util_ss.add(when: 'CONFIG_WIN32', if_true: files('qemu-thread-win32.c'))
util_ss.add(when: 'CONFIG_WIN32', if_true: winmm)
util_ss.add(when: 'CONFIG_WIN32', if_true: pathcch)
if glib_has_gslice
  util_ss.add(files('qtree.c'))
endif
util_ss.add(files('envlist.c', 'path.c', 'module.c'))
util_ss.add(files('host-utils.c'))
util_ss.add(files('bitmap.c', 'bitops.c'))
util_ss.add(files('fifo8.c'))
util_ss.add(files('cacheflush.c'))
util_ss.add(files('error.c', 'error-report.c'))
util_ss.add(files('qemu-print.c'))
util_ss.add(files('id.c'))
util_ss.add(files('qemu-config.c', 'notify.c'))
util_ss.add(files('qemu-option.c', 'qemu-progress.c'))
util_ss.add(files('keyval.c'))
util_ss.add(files('crc32c.c'))
util_ss.add(files('uuid.c'))
util_ss.add(files('getauxval.c'))
util_ss.add(files('rcu.c'))
if have_membarrier
  util_ss.add(files('sys_membarrier.c'))
endif
util_ss.add(files('log.c'))
util_ss.add(files('qdist.c'))
util_ss.add(files('qht.c'))
util_ss.add(files('qsp.c'))
util_ss.add(files('range.c'))
util_ss.add(files('stats64.c'))
util_ss.add(files('systemd.c'))
util_ss.add(files('transactions.c'))
util_ss.add(when: 'CONFIG_POSIX', if_true: files('drm.c'))
util_ss.add(files('guest-random.c'))
util_ss.add(files('yank.c'))
util_ss.add(files('int128.c'))
util_ss.add(files('memalign.c'))
util_ss.add(files('interval-tree.c'))
util_ss.add(files('lockcnt.c'))

if have_user
  util_ss.add(files('selfmap.c'))
endif

if have_system
  util_ss.add(files('crc-ccitt.c'))
  util_ss.add(when: gio, if_true: files('dbus.c'))
  util_ss.add(when: 'CONFIG_LINUX', if_true: files('userfaultfd.c'))
endif

if have_block or have_ga
  util_ss.add(files('aiocb.c', 'async.c'))
  util_ss.add(files('base64.c'))
  util_ss.add(files('main-loop.c'))
  util_ss.add(files('qemu-coroutine.c', 'qemu-coroutine-lock.c', 'qemu-coroutine-io.c'))
  util_ss.add(files(f'coroutine-@coroutine_backend@.c'))
  util_ss.add(files('thread-pool.c', 'qemu-timer.c'))
  util_ss.add(files('qemu-sockets.c'))
endif
if have_block
  util_ss.add(files('aio-wait.c'))
  util_ss.add(files('buffer.c'))
  util_ss.add(files('bufferiszero.c'))
  util_ss.add(files('hbitmap.c'))
  util_ss.add(files('hexdump.c'))
  util_ss.add(files('iova-tree.c'))
  util_ss.add(files('iov.c', 'uri.c'))
  util_ss.add(files('nvdimm-utils.c'))
  util_ss.add(when: 'CONFIG_LINUX', if_true: [
    files('vhost-user-server.c'), vhost_user
  ])
  util_ss.add(files('block-helpers.c'))
  util_ss.add(files('qemu-coroutine-sleep.c'))
  util_ss.add(files('qemu-co-shared-resource.c'))
  util_ss.add(files('qemu-co-timeout.c'))
  util_ss.add(files('readline.c'))
  util_ss.add(files('throttle.c'))
  util_ss.add(files('timed-average.c'))
  if config_host_data.get('CONFIG_INOTIFY1')
    util_ss.add(files('filemonitor-inotify.c'))
  else
    util_ss.add(files('filemonitor-stub.c'))
  endif
  util_ss.add(when: 'CONFIG_LINUX', if_true: files('vfio-helpers.c'))
endif

if cpu == 'aarch64'
  util_ss.add(files('cpuinfo-aarch64.c'))
elif cpu in ['x86', 'x86_64']
  util_ss.add(files('cpuinfo-i386.c'))
elif cpu in ['ppc', 'ppc64']
  util_ss.add(files('cpuinfo-ppc.c'))
endif
