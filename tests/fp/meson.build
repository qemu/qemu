if 'CONFIG_TCG' not in config_all_accel
  subdir_done()
endif
# There are namespace pollution issues on Windows, due to osdep.h
# bringing in Windows headers that define a FLOAT128 type.
if host_os == 'windows'
  subdir_done()
endif

sfcflags = [
  # softfloat defines
  '-DSOFTFLOAT_ROUND_ODD',
  '-DINLINE_LEVEL=5',
  '-DSOFTFLOAT_FAST_DIV32TO16',
  '-DSOFTFLOAT_FAST_DIV64TO32',
  '-DSOFTFLOAT_FAST_INT64',
]
tfcflags = [
  # testfloat defines
  '-DFLOAT16',
  '-DFLOAT64',
  '-DEXTFLOAT80',
  '-DFLOAT128',
  '-DFLOAT_ROUND_ODD',
  '-DLONG_DOUBLE_IS_EXTFLOAT80',
]

libsoftfloat_proj = subproject('berkeley-softfloat-3', required: true,
    default_options: 'defines=' + ','.join(sfcflags))
libsoftfloat = libsoftfloat_proj.get_variable('libsoftfloat_dep')

libtestfloat_proj = subproject('berkeley-testfloat-3', required: true,
    default_options: 'defines=' + ','.join(tfcflags))
libtestfloat = libtestfloat_proj.get_variable('libtestfloat_dep')
libslowfloat = libtestfloat_proj.get_variable('libslowfloat_dep')

fpcflags = [
  # work around TARGET_* poisoning
  '-DHW_POISON_H',
  # define a target to match testfloat's implementation-defined choices, such as
  # whether to raise the invalid flag when dealing with NaNs in muladd.
  '-DTARGET_ARM',
  # FIXME: uiZ may be used uninitialized in this function
  '-Wno-uninitialized',
]

fptest = executable(
  'fp-test',
  ['fp-test.c', '../../fpu/softfloat.c'],
  dependencies: [qemuutil, libsoftfloat, libtestfloat, libslowfloat],
  c_args: fpcflags,
)
softfloat_conv_tests = {
    'float-to-float': 'f16_to_f32 f16_to_f64 f16_to_extF80 f16_to_f128 ' +
                      'f32_to_f16 f32_to_f64 f32_to_extF80 ' +
                      'f64_to_f16 f64_to_f32 ' +
                      'extF80_to_f16 extF80_to_f32 ' +
                      'extF80_to_f64 extF80_to_f128 ' +
                      'f128_to_f16',
    'int-to-float': 'i32_to_f16 i64_to_f16 i32_to_f32 i64_to_f32 ' +
                    'i32_to_f64 i64_to_f64 ' +
                    'i32_to_extF80 i64_to_extF80 ' +
                    'i32_to_f128 i64_to_f128',
    'uint-to-float': 'ui32_to_f16 ui64_to_f16 ui32_to_f32 ui64_to_f32 ' +
                     'ui32_to_f64 ui64_to_f64 ui64_to_f128 ' +
                     'ui32_to_extF80 ui64_to_extF80',
    'float-to-int': 'f16_to_i32 f16_to_i32_r_minMag ' +
                    'f32_to_i32 f32_to_i32_r_minMag ' +
                    'f64_to_i32 f64_to_i32_r_minMag ' +
                    'extF80_to_i32 extF80_to_i32_r_minMag ' +
                    'f128_to_i32 f128_to_i32_r_minMag ' +
                    'f16_to_i64 f16_to_i64_r_minMag ' +
                    'f32_to_i64 f32_to_i64_r_minMag ' +
                    'f64_to_i64 f64_to_i64_r_minMag ' +
                    'extF80_to_i64 extF80_to_i64_r_minMag ' +
                    'f128_to_i64 f128_to_i64_r_minMag',
    'float-to-uint': 'f16_to_ui32 f16_to_ui32_r_minMag ' +
                     'f32_to_ui32 f32_to_ui32_r_minMag ' +
                     'f64_to_ui32 f64_to_ui32_r_minMag ' +
                     'extF80_to_ui32 extF80_to_ui32_r_minMag ' +
                     'f128_to_ui32 f128_to_ui32_r_minMag ' +
                     'f16_to_ui64 f16_to_ui64_r_minMag ' +
                     'f32_to_ui64 f32_to_ui64_r_minMag ' +
                     'f64_to_ui64 f64_to_ui64_r_minMag ' +
                     'extF80_to_ui64 extF80_to_ui64_r_minMag ' +
                     'f128_to_ui64 f128_to_ui64_r_minMag',
    'round-to-integer': 'f16_roundToInt f32_roundToInt ' +
                        'f64_roundToInt extF80_roundToInt f128_roundToInt'
}
softfloat_tests = {
    'eq_signaling' : 'compare',
    'le' : 'compare',
    'le_quiet' : 'compare',
    'lt_quiet' : 'compare',
    'add': 'ops',
    'sub': 'ops',
    'mul': 'ops',
    'div': 'ops',
    'rem': 'ops',
    'sqrt': 'ops'
}
# The full test suite can take a bit of time, default to a quick run
# "-l 2 -r all" can take more than a day for some operations and is best
# run manually
fptest_args = ['-q', '-s', '-l', '1']
fptest_rounding_args = ['-r', 'all']

# Conversion Routines:
foreach k, v : softfloat_conv_tests
  test('fp-test-' + k, fptest,
       args: fptest_args + fptest_rounding_args + v.split(),
       suite: ['softfloat', 'softfloat-conv'])
endforeach

foreach k, v : softfloat_tests
  test('fp-test-' + k, fptest,
       args: fptest_args + fptest_rounding_args +
             ['f16_' + k, 'f32_' + k, 'f64_' + k, 'f128_' + k, 'extF80_' + k],
       suite: ['softfloat', 'softfloat-' + v])
endforeach

# FIXME: extF80_{mulAdd} (missing)
test('fp-test-mulAdd', fptest,
     # no fptest_rounding_args
     args: fptest_args +
           ['f16_mulAdd', 'f32_mulAdd', 'f64_mulAdd', 'f128_mulAdd'],
     suite: ['softfloat-slow', 'softfloat-ops-slow', 'slow'], timeout: 180)

executable(
  'fp-bench',
  ['fp-bench.c', '../../fpu/softfloat.c'],
  dependencies: [qemuutil, libtestfloat, libsoftfloat],
  c_args: fpcflags,
)

fptestlog2 = executable(
  'fp-test-log2',
  ['fp-test-log2.c', '../../fpu/softfloat.c'],
  dependencies: [qemuutil, libsoftfloat],
  c_args: fpcflags,
)
test('fp-test-log2', fptestlog2,
     suite: ['softfloat', 'softfloat-ops'])
