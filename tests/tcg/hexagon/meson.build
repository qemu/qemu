tests = []

cflags = ['-Wno-incompatible-pointer-types', '-Wno-undefined-internal',
          '-fno-unroll-loops', '-fno-stack-protector']
asmflags = [cflags, '-nostdlib', '-mv67', files('crt.S')]

multi_src = []
foreach t: tcg_tests['multiarch-linux-user']['tests']
  foreach src, setup: t
    if 'gdb_test' in setup
      # gdb does not support hexagon, ignore test
      continue
    endif
    new_cflags = []
    if 'cflags' in setup
      new_cflags += setup['cflags']
    endif
    new_cflags += cflags
    if src not in multi_src
      setup += {'cflags': [new_cflags]}
      multi_src += src
    endif
    tests += {src: setup}
  endforeach
endforeach

tests += {
  'atomics.c': {'cflags': cflags},
  'brev.c': {'cflags': cflags},
  # Compile for v66 so that the ELF selects a v66 CPU; the test then
  # exercises revision gating by executing a v68 .word instruction.
  'check_rev_gating.c': {
    'cflags': [cflags, '-mv66', '-O2'],
    'qemu_args': ['-cpu', 'v66'],
  },
  'circ.c': {'cflags': cflags},
  'first.S': {'cflags': asmflags},
  'fpstuff.c': {'cflags': cflags},
  'hex_sigsegv.c': {'cflags': cflags},
  'fp_hvx.c': {'cflags': [cflags, '-mv73', '-mhvx', '-mhvx-ieee-fp']},
  'fp_hvx_disabled.c': {'cflags': [cflags, '-mv73', '-mhvx', '-mhvx-ieee-fp'],
                        'qemu_args': ['-cpu', 'v73,ieee-fp=false']},
  'fp_hvx_cvt.c': {'cflags': [cflags, '-mv73', '-mhvx', '-mhvx-ieee-fp']},
  'fp_hvx_cmp.c': {'cflags': [cflags, '-mv73', '-mhvx', '-mhvx-ieee-fp']},
  'hvx_misc.c': {'cflags': [cflags, '-mhvx', '-mv69']},
  'hvx_histogram.c': {
    'cflags': [cflags, '-mhvx', '-Wno-gnu-folding-constant',
               files('hvx_histogram_row.S')]
  },
  'invalid-encoding.c': {'cflags': cflags},
  'invalid-slots.c': {'cflags': cflags},
  'icinva.c': {'cflags': cflags},
  'load_align.c': {'cflags': cflags},
  'load_unpack.c': {'cflags': cflags},
  # Build this test with -mv71 to exercise the CABAC instruction
  'misc.c': {'cflags': [cflags, '-mv71', '-O2']},
  'multiple-writes.c': {'cflags': cflags},
  'overflow.c': {'cflags': cflags, 'exe_name': 'hex-overflow'},
  'preg_alias.c': {'cflags': cflags},
  'dual_stores.c': {'cflags': cflags},
  'multi_result.c': {'cflags': cflags},
  'mem_noshuf.c': {'cflags': cflags},
  'mem_noshuf_exception.c': {'cflags': cflags},
  'privileged-insn.c': {'cflags': cflags},
  'read_write_overlap.c': {'cflags': cflags},
  'reg_mut.c': {'cflags': cflags},
  'scatter_gather.c': {'cflags': [cflags, '-mhvx']},
  'signal_context.c': {'cflags': cflags},
  'test_abs.S': {'cflags': asmflags},
  'test_bitcnt.S': {'cflags': asmflags},
  'test_bitsplit.S': {'cflags': asmflags},
  'test_call.S': {'cflags': asmflags},
  'test_clobber.S': {'cflags': asmflags},
  'test_cmp.S': {'cflags': asmflags},
  'test_dotnew.S': {'cflags': asmflags},
  'test_ext.S': {'cflags': asmflags},
  'test_fibonacci.S': {'cflags': asmflags},
  'test_hl.S': {'cflags': asmflags},
  'test_hwloops.S': {'cflags': asmflags},
  'test_jmp.S': {'cflags': asmflags},
  'test_lsr.S': {'cflags': asmflags},
  'test_mpyi.S': {'cflags': asmflags},
  'test_packet.S': {'cflags': asmflags},
  'test_reorder.S': {'cflags': asmflags},
  'test_round.S': {'cflags': asmflags},
  'test_vavgw.S': {'cflags': asmflags},
  'test_vcmpb.S': {'cflags': asmflags},
  'test_vcmpw.S': {'cflags': asmflags},
  'test_vlsrw.S': {'cflags': asmflags},
  'test_vmaxh.S': {'cflags': asmflags},
  'test_vminh.S': {'cflags': asmflags},
  'test_vpmpyh.S': {'cflags': asmflags},
  'test_vspliceb.S': {'cflags': asmflags},
  'unaligned_pc.c': {'cflags': cflags},
  'unaligned_data.c': {'cflags': cflags},
  'usr.c': {
    'cflags': [cflags, '-mv67t', '-O2',
               '-Wno-inline-asm', '-Wno-expansion-to-defined']
  },
  'v68_scalar.c': {'cflags': [cflags, '-mv68']},
  'v68_hvx.c': {'cflags': [cflags, '-mhvx', '-Wno-unused-function', '-mv68']},
  'v69_hvx.c': {'cflags': [cflags, '-mhvx', '-Wno-unused-function', '-mv69']},
  'v73_scalar.c': {'cflags': [cflags, '-Wno-unused-function', '-mv73']},
  'valid-slots.c': {'cflags': cflags},
  'vector_add_int.c': {'cflags': [cflags, '-mhvx', '-fvectorize']},
}

if 'qemu-hexagon' in emulators
  tcg_tests += {
    'hexagon-linux-user': {
      'cc': 'hexagon-unknown-linux-musl-clang',
      'cc_dockerfile': 'debian-hexagon-cross',
      'cc_docker_host_arch': ['x86_64'],
      'folder': 'hexagon',
      'gdb_arch': 'hexagon_is_not_supported_by_gdb',
      'qemu': emulators['qemu-hexagon'],
      'tests': tests,
    }
  }
endif
