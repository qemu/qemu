tests = []

minilib_dir = meson.current_source_dir() / '..' / '..' / 'minilib'
minilib_printf = files(minilib_dir / 'printf.c')[0]
link_script = files('../softmmu.ld')[0]
head64 = files('../head64.S')[0]
console = files('../console.c')[0]
ipl_inc = meson.project_source_root()/'include'/'hw'/'s390x'/'ipl'
cflags = ['-nostdlib',
          '-ffreestanding',
          '-Wa,--noexecstack',
          '-I', minilib_dir, minilib_printf,
          '-I', ipl_inc,
          '-march=z13',
          head64, console]
asmflags = ['-nostdlib',
            '-Wl,-T', link_script, '-Wl,--build-id=none',
            '-march=z13', '-Wa,--noexecstack']
qemu_args = ['-display', 'none',
             '-action', 'panic=exit-failure',
             '-serial', 'stdio',
             '-kernel']

# Multi arch tests - add cflags only once per src
multi_src = []
foreach t: tcg_tests['multiarch-softmmu']['tests']
  foreach src, setup: t
    new_cflags = cflags
    if fs.stem(src) == 'memory'
      new_cflags += '-DCHECK_UNALIGNED=0'
    endif
    if 'test_name' in setup and setup['test_name'] == 'memory-access'
      # s390x clears the BSS section so we need to account for that
      # ./validate-memory-counts.py --bss-cleared
      setup += {'wrapper': setup['wrapper'] + '--bss-cleared'}
    endif
    if src not in multi_src
      setup += {'cflags': [new_cflags]}
      multi_src += src
    endif
    tests += {src: setup + {'qemu_args': qemu_args}}
  endforeach
endforeach

tests += {
  'mvc-smc.c': {'cflags': [cflags], 'qemu_args': qemu_args},
}

setup_asm = {'cflags': [asmflags], 'qemu_args': qemu_args}
tests += {
  'bal.S': setup_asm,
  'cksm.S': setup_asm,
  'clm.S': setup_asm,
  'exrl-ssm-early.S': setup_asm,
  'icm.S': setup_asm,
  'sam.S': setup_asm,
  'lpsw.S': setup_asm,
  'lpswe-early.S': setup_asm,
  'lra.S': setup_asm,
  'mc.S': setup_asm,
  'per.S': setup_asm,
  'precise-smc-softmmu.S': setup_asm,
  'sckc.S': setup_asm,
  'ssm-early.S': setup_asm,
  'stosm-early.S': setup_asm,
  'stpq.S': setup_asm,
  'unaligned-lowcore.S': setup_asm,
}

foreach src: tests_s390x_pgm
  tests += {
    src: {
      'cflags': [asmflags, files('../pgm-specification-softmmu.S')],
      'qemu_args': qemu_args,
    }
  }
endforeach

if 'qemu-system-s390x' in emulators
  tcg_tests += {
    's390x-softmmu': {
      'cc': cc,
      'cc_dockerfile': cc_dockerfile,
      'cc_docker_host_arch': cc_docker_host_arch,
      'cc_feat_cflags': cc_feat_cflags,
      'folder': 's390x',
      'gdb_arch': 's390x',
      'qemu': emulators['qemu-system-s390x'],
      'tests': tests,
    }
  }
endif
