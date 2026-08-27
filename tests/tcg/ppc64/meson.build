cc_feat_cflags = {
  'power8': '-mcpu=power8',
  'power10': '-mcpu=power10',
}

tests = []

# Multi arch tests
foreach t: tcg_tests['multiarch-linux-user']['tests']
  foreach src, setup: t
    # ppc64 ABI uses function descriptors, and thus, QEMU can't find symbol
    # for a given instruction. Thus, skip this test.
    if fs.stem(src) == 'test-plugin-mem-access' and 'wrapper' in setup
      continue
    endif
    tests += {src: setup}
  endforeach
endforeach

tests += {
  '../ppc64/bcdsub.c': {'cc_feat': 'power8'},
  '../ppc64/byte_reverse.c': {'cc_feat': 'power10', 'qemu_args': ['-cpu', 'POWER10']},
  '../ppc64/mffsce.c': {},
  '../ppc64/mtfsf.c': {},
  '../ppc64/non_signalling_xscv.c': {'cc_feat': 'power8'},
  '../ppc64/signal_save_restore_xer.c': {},
  '../ppc64/test-aes.c': {},
  '../ppc64/vector.c': {'cc_feat': 'power10', 'qemu_args': ['-cpu', 'POWER10']},
  '../ppc64/vsx_f2i_nan.c': {'cc_feat': 'power8'},
  '../ppc64/xxspltw.c': {},
  '../multiarch/sha512.c': {
    'exe_name': 'sha512-vector',
    'cc_feat': 'power10',
    'cflags': ['-O3'],
    'qemu_args': ['-cpu', 'POWER10'],
  }
}

# export tests for ppc64le
tcg_tests_ppc64 = tests

if 'qemu-ppc64' in emulators
  tcg_tests += {
    'ppc64-linux-user': {
      'cc': 'powerpc64-linux-gnu-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'cc_docker_host_arch': ['x86_64'],
      'cc_feat_cflags': cc_feat_cflags,
      'folder': 'ppc64',
      'gdb_arch': 'ppc64',
      'gdb_feat_version': gdb_feat_version,
      'qemu': emulators['qemu-ppc64'],
      'tests': tests,
    }
  }
endif
