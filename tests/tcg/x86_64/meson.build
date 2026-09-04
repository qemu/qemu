cc = 'x86_64-linux-gnu-gcc'
cc_dockerfile = 'debian-amd64-cross'
cc_docker_host_arch = ['x86_64']

subdir('system')

tests = []

tests += tcg_tests['multiarch-linux-user']['tests']

test_3dnow_h = custom_target('test_3dnow_h',
                             command: [files('../i386/test-mmx.py'),
                                       files('../i386/x86.csv'),
                                       '@OUTPUT@',
                                       '3DNOW'],
                             output: 'test-3dnow.h')

test_avx_h = custom_target('test_avx_h',
                           command: [files('../i386/test-avx.py'),
                                     files('../i386/x86.csv'),
                                     '@OUTPUT@'],
                           output: 'test-avx.h')

test_mmx_h = custom_target('test_mmx_h',
                           command: [files('../i386/test-mmx.py'),
                                     files('../i386/x86.csv'),
                                     '@OUTPUT@',
                                     'MMX', 'SSE', 'SSE2', 'SSE3', 'SSSE3'],
                           output: 'test-mmx.h')

tests += {
  '../i386/test-i386-ssse3.c': {},
  '../i386/test-3dnow.c': {
    'cflags': ['-I', meson.current_build_dir(),
               '-masm=intel', '-O'],
    'depends': [test_3dnow_h],
  },
  '../i386/test-avx.c': {
    'cflags': ['-I', meson.current_build_dir(),
               '-mavx', '-masm=intel', '-O'],
    'depends': [test_avx_h],
  },
  '../i386/test-flags.c': {},
  '../i386/test-mmx.c': {
    'cflags': ['-I', meson.current_build_dir(),
               '-masm=intel', '-O'],
    'depends': [test_mmx_h],
  },
  '../i386/test-i386.c': {
    'cflags': ['-lm', '-lc'],
    'exe_name': 'test-x86_64'
  },
  '../i386/test-i386-adcox.c': {'cflags': ['-O2']},
  '../i386/test-i386-bmi2.c': {'cflags': ['-O2', '-fwrapv']},
}

tests += {
  'adox.c': {'cflags': ['-O2']},
  'cmpxchg.c': {},
  'cross-modifying-code.c': {
    'cflags': ['-pthread', '-Wl,--no-warn-rwx-segments']
  },
  'fma.c': {},
  'noexec.c': {},
  'segment-prefixes.c': {},
  'test-1648.c': {},
  'test-2175.c': {},
  'test-2413.c': {},
  'vsyscall.c': {},
}

if 'qemu-x86_64' in emulators
  tcg_tests += {
    'x86_64-linux-user': {
      'cc': cc,
      'cc_dockerfile': cc_dockerfile,
      'cc_docker_host_arch': cc_docker_host_arch,
      'folder': 'x86_64',
      'gdb_arch': 'x86_64',
      'qemu': emulators['qemu-x86_64'],
      'tests': tests,
    }
  }
endif
