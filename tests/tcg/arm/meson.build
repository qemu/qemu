cc = 'arm-linux-gnueabihf-gcc'
cc_dockerfile = 'debian-all-test-cross'
cc_docker_host_arch = ['aarch64', 'x86_64']

tests = []

# Multi arch tests
multi_src = []
foreach t: tcg_tests['multiarch-linux-user']['tests']
  foreach src, setup: t
    new_cflags = []
    if 'cflags' in setup
      new_cflags = setup['cflags']
    endif
    if fs.stem(src) == 'float_madds'
      new_cflags += ['-mfpu=neon-vfpv4']
    elif fs.stem(src) == 'test-plugin-set-pc'
      # Require emitting arm32 insns, otherwise the vCPU might accidentally
      # try to execute Thumb insns in arm32 mode after qemu_plugin_set_pc()
      new_cflags += ['-marm']
    endif
    if src not in multi_src
      setup += {'cflags': new_cflags}
      multi_src += src
    endif
    tests += {src: setup}
  endforeach
endforeach

tests += {
  'commpage.c': {},
  'hello-arm.c': {
    'cflags': ['-marm', '-nostdlib', '-ffreestanding', '-fno-stack-protector'],
  },
  'fcvt.c': {
    'cflags': ['-lm', '-march=armv8.2-a+fp16', '-mfpu=neon-fp-armv8'],
    'expected_output': 'fcvt.ref',
  },
  'pcalign-a32.c': {'cflags': ['-marm']},
}

# Vector SHA1
# Work around compiler false-positive warning, as we do for the 'sha1' test
tests += {'../multiarch/sha1.c': {
            'exe_name': 'sha1-vector',
            'cflags': ['-O3', '-Wno-stringop-overread'],
            'expected_output': '../multiarch/sha1.ref',
         }}

# Vector versions of sha512 (-O3 triggers vectorisation)
tests += {'../multiarch/sha512.c': {
            'exe_name': 'sha512-vector',
            'cflags': ['-O3'],
         }}

# Semihosting tests
semihosting_cflags = ['-marm', '-mthumb', '-I', meson.current_source_dir()]
tests += {
  '../multiarch/arm-compat-semi/semiconsole.c': {
    'cflags': semihosting_cflags,
    'wrapper': [prog_run_with_input, 'X'],
  },
  '../multiarch/arm-compat-semi/semihosting.c': {'cflags': semihosting_cflags},
}

if 'qemu-arm' in emulators
  tcg_tests += {
    'arm-linux-user': {
      'cc': cc,
      'cc_dockerfile': cc_dockerfile,
      'cc_docker_host_arch': cc_docker_host_arch,
      'folder': 'arm',
      'gdb_arch': 'arm',
      'qemu': emulators['qemu-arm'],
      'tests': tests,
    }
  }
endif
