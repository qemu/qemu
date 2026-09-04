tests = []

minilib_dir = meson.current_source_dir() / '..' / '..' / 'minilib'
minilib_printf = files(minilib_dir / 'printf.c')[0]
link_script = files('kernel.ld')[0]
boot = files('boot.S')[0]
cflags = ['-nostdlib',
          '-ffreestanding',
          '-Wa,--noexecstack',
          '-I', minilib_dir, minilib_printf, boot,
          '-Wl,-T', link_script,
          '-march=armv7-a+fp',
          '-lgcc']
qemu_base_args = ['-display', 'none',
                  '-semihosting-config', 'enable=on',
                  '-kernel']
qemu_def_args = ['-M', 'virt', '-cpu', 'max', qemu_base_args]

# Multi arch tests
multi_src = []
foreach t: tcg_tests['multiarch-softmmu']['tests']
  foreach src, setup: t
    new_cflags = cflags
    if fs.stem(src) == 'memory'
      new_cflags += '-DCHECK_UNALIGNED=0'
    endif
    if src not in multi_src
      setup += {'cflags': [new_cflags]}
      multi_src += src
    endif
    tests += {src: setup + {'qemu_args': qemu_def_args}}
  endforeach
endforeach

tests += {
  'test-armv6m-undef.S': {
    'cflags': ['-mcpu=cortex-m0', '-mfloat-abi=soft', '-nostdlib',
               '-Wl,--build-id=none',
               '-T', files('test-armv6m-undef.ld')],
    'qemu_args': ['-M', 'microbit', qemu_base_args],
  },
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
}

if 'qemu-system-arm' in emulators
  tcg_tests += {
    'arm-softmmu': {
      'cc': cc,
      'cc_dockerfile': cc_dockerfile,
      'cc_docker_host_arch': cc_docker_host_arch,
      'folder': 'arm/system',
      'gdb_arch': 'arm',
      'qemu': emulators['qemu-system-arm'],
      'tests': tests,
    }
  }
endif
