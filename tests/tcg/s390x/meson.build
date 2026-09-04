cc = 's390x-linux-gnu-gcc'
cc_dockerfile = 'debian-all-test-cross'
cc_docker_host_arch = ['aarch64', 'x86_64']

cc_feat_cflags = {
  'z14': '-march=z14',
  'z15': '-march=z15',
}

tests_s390x_pgm = [
  'br-odd.S',
  'cgrl-unaligned.S',
  'clrl-unaligned.S',
  'crl-unaligned.S',
  'ex-odd.S',
  'lgrl-unaligned.S',
  'llgfrl-unaligned.S',
  'lpswe-unaligned.S',
  'lrl-unaligned.S',
  'stgrl-unaligned.S',
  'strl-unaligned.S',
]

subdir('system')

tests = []

# Multi arch tests
tests += tcg_tests['multiarch-linux-user']['tests']

tests += {
  # The following tests contain inline assembly that requires inlining,
  # and thus cannot be built with -O0.
  'rxsbg.c': {'cflags': ['-O2']},
  'divide-to-integer.c': {'cflags': ['-O2', '-lm']},

  'hello-s390x.c': {},
  'hello-s390x-asm.S': {'cflags': ['-nostdlib']},
  'csst.c': {},
  'ipm.c': {},
  'exrl-trt.c': {},
  'exrl-trtr.c': {},
  'pack.c': {},
  'mie3-compl.c': {},
  'mie3-mvcrl.c': {},
  'mie3-sel.c': {},
  'mvo.c': {},
  'mvc.c': {},
  'shift.c': {},
  'trap.c': {},
  'signals-s390x.c': {},
  'branch-relative-long.c': {},
  'noexec.c': {},
  'div.c': {},
  'clst.c': {},
  'long-double.c': {},
  'cdsg.c': {},
  'chrl.c': {},
  'ex-relative-long.c': {},
  'ex-branch.c': {},
  'mxdb.c': {},
  'epsw.c': {},
  'larl.c': {},
  'mdeb.c': {},
  'cgebra.c': {'cflags': ['-lm']},
  'clgebr.c': {'cflags': ['-lm']},
  'clc.c': {},
  'laalg.c': {},
  'add-logical-with-carry.c': {},
  'lae.c': {},
  'cvd.c': {},
  'cvb.c': {},
  'stckf.c': {},
  'ts.c': {},
  'ex-smc.c': {'cflags': ['-Wl,--no-warn-rwx-segments']},
}

foreach src: tests_s390x_pgm
  tests += {
    src: {'cflags': [files('pgm-specification-user.c'), '-Wa,--noexecstack']}
  }
endforeach

setup_z13 = {'cflags': ['-O2', '-march=z13']}
tests += {
  'vistr.c': setup_z13,
  'lcbb.c': setup_z13,
  'locfhr.c': setup_z13,
  'vcksm.c': setup_z13,
  'vstl.c': setup_z13,
  'vrep.c': setup_z13,
  'precise-smc-user.c': setup_z13,
  'prno-trng.c': setup_z13,
  '../multiarch/sha512.c': {
    'exe_name': 'sha512-mvx',
    'cflags': setup_z13['cflags'] + ['-mvx', '-O3'],
  }
}

setup_z14 = {
  'cc_feat': 'z14',
  'cflags': ['-O2', '-lm', '-Wl,--no-warn-rwx-segments']
}
tests += {
  'fma.c': setup_z14,
  'vfminmax.c': setup_z14,
}

setup_z15 = {'cc_feat': 'z15', 'cflags': ['-O2']}
tests += {
  'vxeh2_vs.c': setup_z15,
  'vxeh2_vcvt.c': setup_z15,
  'vxeh2_vlstr.c': setup_z15,
  'vxeh2_vstrs.c': setup_z15,
}

tests += {
  'signals-s390x.c': {
    'test_name': 'signals',
    'gdb_test': ['--test', files('gdbstub/test-signals-s390x.py')],
  },
  'hello-s390x-asm.S': {
    'test_name': 'svc',
    'gdb_test': ['--test', files('gdbstub/test-svc.py')],
  },
}

if 'qemu-s390x' in emulators
  tcg_tests += {
    's390x-linux-user': {
      'cc': cc,
      'cc_dockerfile': cc_dockerfile,
      'cc_docker_host_arch': cc_docker_host_arch,
      'cc_feat_cflags': cc_feat_cflags,
      'folder': 's390x',
      'gdb_arch': 's390x',
      'gdb_feat_version': gdb_feat_version,
      'qemu': emulators['qemu-s390x'],
      'tests': tests,
    }
  }
endif
