tests = []

minilib_dir = meson.current_source_dir() / '..' / '..' / 'minilib'
minilib_printf = files(minilib_dir / 'printf.c')[0]
link_script = files('kernel.ld')[0]
boot = files('boot.S')[0]
cflags = ['-nostdlib',
          '-ffreestanding',
          '-Wa,--noexecstack',
          '-I', minilib_dir, minilib_printf, boot,
          '-Wl,-T', link_script]
qemu_base_args = ['-display', 'none',
                  '-semihosting-config', 'enable=on',
                  '-kernel']
qemu_def_args = ['-M', 'virt', '-cpu', 'max', qemu_base_args]

# Multi arch tests - add cflags only once per src
multi_src = []
foreach t: tcg_tests['multiarch-softmmu']['tests']
  foreach src, setup: t
    new_cflags = cflags
    if fs.stem(src) == 'memory'
      new_cflags += '-DCHECK_UNALIGNED=1'
    endif
    if src not in multi_src
      setup += {'cflags': [new_cflags]}
      multi_src += src
    endif
    tests += {src: setup + {'qemu_args': qemu_def_args}}
  endforeach
endforeach

tests += {
  '../../multiarch/system/memory.c': {
    'exe_name': 'memory-sve',
    'cc_feat': 'sve',
    'cflags': [cflags, '-DCHECK_UNALIGNED=1', '-O3'],
    'qemu_args': qemu_def_args,
  },
}

tests += {
  'asid2.c': {'cflags': cflags, 'qemu_args': qemu_def_args},
  'feat-xs.c': {'cflags': cflags, 'qemu_args': qemu_def_args},
  'gpc-test.c': {
    'cflags': [cflags, '-DLOGGING_VECTOR_TABLE',
               '-Wno-main', '-Wno-unused-but-set-variable'],
    'qemu_args': ['-M', 'virt,secure=on,virtualization=on,gic-version=3',
                  '-cpu', 'max,x-rme=on',
                  '-semihosting-config', 'enable=on,arg=3',
                  qemu_base_args],
  },
  'rme_gdi.c': {'cflags': cflags, 'qemu_args': qemu_def_args},
  'semiheap.c': {'cflags': cflags, 'qemu_args': qemu_def_args},
  'semiconsole.c': {
    'cflags': cflags,
    'qemu_args': ['-serial', 'none', '-chardev', 'stdio,mux=on,id=stdio0',
                  '-semihosting-config', 'enable=on,chardev=stdio0',
                  '-mon', 'chardev=stdio0,mode=readline',
                  qemu_def_args],
    'wrapper': [prog_run_with_input, 'X']
  },
  '../../multiarch/system/memory.c': {
    'test_name': 'memory-record-replay',
    'qemu_args': qemu_def_args,
    'wrapper': [prog_record_replay],
  },
  'vtimer.c': {
    'cflags': cflags,
    'qemu_args': ['-M', 'virt,virtualization=on,gic-version=2',
                  '-cpu', 'cortex-a57', '-smp', '4',
                  '-semihosting-config', 'enable=on,arg=2',
                  qemu_base_args]
  },
}

tests += {
  'pauth-3.c': {
    'cc_feat': 'v8.3',
    'cflags': cflags,
    'qemu_args': ['-M', 'virt', '-cpu', 'max,pauth-qarma5=on', qemu_base_args],
  }
}

tests += {
  'mte.S': {
    'cc_feat': 'mte',
    'cflags': cflags,
    'qemu_args': ['-M', 'virt,mte=on', '-cpu', 'max', qemu_base_args],
    'gdb_test': ['--test', files('../gdbstub/test-mte.py'), '--', '--mode=system'],
    'gdb_feat': 'mte-baremetal',
  }
}

if 'qemu-system-aarch64' in emulators
  tcg_tests += {
    'aarch64-softmmu': {
      'cc': cc,
      'cc_dockerfile': cc_dockerfile,
      'cc_docker_host_arch': cc_docker_host_arch,
      'cc_feat_cflags': cc_feat_cflags,
      'folder': 'aarch64/system',
      'gdb_arch': 'aarch64',
      'gdb_feat_version': gdb_feat_version,
      'qemu': emulators['qemu-system-aarch64'],
      'tests': tests,
    }
  }
endif
