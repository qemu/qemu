cc = 'i686-linux-gnu-gcc'
cc_dockerfile = 'debian-i686-cross'
cc_docker_host_arch = ['x86_64']

tests = []

tests += tcg_tests['multiarch-linux-user']['tests']

tests += {
  'hello-i386.c': {
    'cflags': ['-ffreestanding', '-fno-stack-protector', '-nostdlib'],
  },
  'test-aes.c': {'cflags': ['-O1', '-msse2', '-maes']},
  'test-i386-adcox.c': {'cflags': ['-O2']},
  'test-i386-bmi2.c': {'cflags': ['-O2', '-fwrapv']},
  'test-i386-f2xm1.c': {},
  'test-i386-fbstp.c': {},
  'test-i386-fisttp.c': {},
  'test-i386-fldcst.c': {},
  'test-i386-fp-exceptions.c': {},
  # TODO: original test-i386-fprem test was comparing output between qemu and
  # bare-metal. At the moment, files are not matching, so deactivate compare.
  'test-i386-fprem.c': {},
  'test-i386-fpatan.c': {},
  'test-i386-fscale.c': {},
  'test-i386-fxam.c': {},
  'test-i386-fxtract.c': {},
  'test-i386-fyl2x.c': {},
  'test-i386-fyl2xp1.c': {},
  'test-i386-pcmpistri.c': {'cflags': ['-msse4.2']},
  'test-i386-pseudo-denormal.c': {},
  'test-i386-snan-convert.c': {},
  'test-i386-sse-exceptions.c': {'cflags': ['-msse4.1', '-mfpmath=sse']},
  'test-i386.c': {
    'cflags': [files('test-i386-code16.S'), files('test-i386-vm86.S'),
               '-lm', '-fno-pie', '-Wl,--no-warn-rwx-segments'],
  },
  '../multiarch/sha512.c': {
     'exe_name': 'sha512-sse',
     'cflags': ['-msse4.1', '-O3'],
  },
}

if 'qemu-i386' in emulators
  tcg_tests += {
    'i386-linux-user': {
      'cc': cc,
      'cc_dockerfile': cc_dockerfile,
      'cc_docker_host_arch': cc_docker_host_arch,
      'folder': 'i386',
      'gdb_arch': 'i386',
      'qemu': emulators['qemu-i386'],
      'tests': tests,
    }
  }
endif
