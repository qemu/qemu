# If possible, add new files to other directories, by using "if_false".
# If you need them here, try to add them under one of the if statements
# below, so that it is clear who needs the stubbed functionality.

stub_ss.add(files('cpu-get-clock.c'))
stub_ss.add(files('error-printf.c'))
stub_ss.add(files('fdset.c'))
stub_ss.add(files('iothread-lock.c'))
stub_ss.add(files('is-daemonized.c'))
stub_ss.add(files('monitor-core.c'))
stub_ss.add(files('replay-mode.c'))
stub_ss.add(files('trace-control.c'))

if have_block
  stub_ss.add(files('bdrv-next-monitor-owned.c'))
  stub_ss.add(files('blk-commit-all.c'))
  stub_ss.add(files('blk-exp-close-all.c'))
  stub_ss.add(files('blockdev-close-all-bdrv-states.c'))
  stub_ss.add(files('change-state-handler.c'))
  stub_ss.add(files('get-vm-name.c'))
  stub_ss.add(files('iothread-lock-block.c'))
  stub_ss.add(files('migr-blocker.c'))
  stub_ss.add(files('physmem.c'))
  stub_ss.add(files('ram-block.c'))
  stub_ss.add(files('runstate-check.c'))
  stub_ss.add(files('uuid.c'))
endif

if have_block or have_ga
  stub_ss.add(files('replay-tools.c'))
  # stubs for hooks in util/main-loop.c, util/async.c etc.
  stub_ss.add(files('cpus-virtual-clock.c'))
  stub_ss.add(files('icount.c'))
  stub_ss.add(files('graph-lock.c'))
  if libaio.found()
    stub_ss.add(files('linux-aio.c'))
  endif
  stub_ss.add(files('qemu-timer-notify-cb.c'))

  # stubs for monitor
  stub_ss.add(files('monitor-internal.c'))
  stub_ss.add(files('qmp-command-available.c'))
  stub_ss.add(files('qmp-quit.c'))
endif

if have_block or have_user
  stub_ss.add(files('qtest.c'))
  stub_ss.add(files('vm-stop.c'))
  stub_ss.add(files('vmstate.c'))
endif

if have_user
  # Symbols that are used by hw/core.
  stub_ss.add(files('cpu-synchronize-state.c'))
  stub_ss.add(files('cpu-destroy-address-spaces.c'))

  # Stubs for QAPI events.  Those can always be included in the build, but
  # they are not built at all for --disable-system builds.
  if not have_system
    stub_ss.add(files('qdev.c'))
  endif

  stub_ss.add(files('monitor-internal.c'))
endif

if have_system
  # Symbols that are only needed in some configurations.  Try not
  # adding more of these.  If the symbol is used in specific_ss,
  # in particular, consider defining a preprocessor macro via
  # Kconfig or configs/targets/.
  stub_ss.add(files('dump.c'))
  stub_ss.add(files('cmos.c'))
  stub_ss.add(files('fw_cfg.c'))
  stub_ss.add(files('igvm.c'))
  stub_ss.add(files('target-get-monitor-def.c'))
  stub_ss.add(files('target-monitor-defs.c'))
  stub_ss.add(files('win32-kbd-hook.c'))
  stub_ss.add(files('xen-hw-stub.c'))
  stub_ss.add(files('monitor-arm-gic.c'))
  stub_ss.add(files('monitor-i386-rtc.c'))
  stub_ss.add(files('monitor-i386-sev.c'))
  stub_ss.add(files('monitor-i386-sgx.c'))
  stub_ss.add(files('monitor-i386-xen.c'))
  stub_ss.add(files('monitor-cpu.c'))
  stub_ss.add(files('monitor-cpu-s390x.c'))
  stub_ss.add(files('monitor-cpu-s390x-kvm.c'))
endif

if have_system or have_user
  stub_ss.add(files('gdbstub.c'))

  # Also included in have_system for --disable-tcg builds
  stub_ss.add(files('replay.c'))

  # Also included in have_system for tests/unit/test-qdev-global-props
  stub_ss.add(files('hotplug-stubs.c'))
  stub_ss.add(files('sysbus.c'))
endif
