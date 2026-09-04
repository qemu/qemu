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
          '-mcpu=ev6',
          '-Wl,--no-warn-rwx-segments',
          '-lgcc']
qemu_def_args = ['-display', 'none',
                 '-serial', 'stdio',
                 '-kernel']

# Multi arch tests
multi_src = []
foreach t: tcg_tests['multiarch-softmmu']['tests']
  foreach src, setup: t
    new_cflags = cflags
    if fs.stem(src) == 'memory'
      new_cflags += ['-DCHECK_UNALIGNED=0', '-mbwx']
    endif
    if src not in multi_src
      setup += {'cflags': [new_cflags]}
      multi_src += src
    endif
    tests += {src: setup + {'qemu_args': qemu_def_args}}
  endforeach
endforeach

if 'qemu-system-alpha' in emulators
  tcg_tests += {
    'alpha-softmmu': {
      'cc': cc,
      'cc_dockerfile': cc_dockerfile,
      'cc_docker_host_arch': cc_docker_host_arch,
      'folder': 'alpha/system',
      'gdb_arch': 'alpha',
      'qemu': emulators['qemu-system-alpha'],
      'tests': tests,
    }
  }
endif
