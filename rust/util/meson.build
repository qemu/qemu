_util_bindgen_args = []
c_enums = [
  'module_init_type',
  'QEMUClockType',
]
foreach enum : c_enums
  _util_bindgen_args += ['--rustified-enum', enum]
endforeach

#
# TODO: Remove this comment when the clang/libclang mismatch issue is solved.
#
# Rust bindings generation with `bindgen` might fail in some cases where the
# detected `libclang` does not match the expected `clang` version/target. In
# this case you must pass the path to `clang` and `libclang` to your build
# command invocation using the environment variables CLANG_PATH and
# LIBCLANG_PATH
_util_bindings_inc_rs = rust.bindgen(
  input: 'wrapper.h',
  dependencies: common_ss.all_dependencies(),
  output: 'bindings.inc.rs',
  include_directories: bindings_incdir,
  bindgen_version: ['>=0.60.0'],
  args: bindgen_args_common + _util_bindgen_args,
  c_args: bindgen_c_args,
)

_util_rs = static_library(
  'util',
  structured_sources(
    [
      'src/lib.rs',
      'src/bindings.rs',
      'src/error.rs',
      'src/log.rs',
      'src/module.rs',
      'src/timer.rs',
    ],
    {'.': _util_bindings_inc_rs}
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  dependencies: [anyhow_rs, libc_rs, foreign_rs, glib_sys_rs, common_rs, qom, qemuutil],
)

util_rs = declare_dependency(link_with: [_util_rs])

rust.test('rust-util-tests', _util_rs,
          dependencies: [qemuutil, qom],
          suite: ['unit', 'rust'])

# Doctests are essentially integration tests, so they need the same dependencies.
# Note that running them requires the object files for C code, so place them
# in a separate suite that is run by the "build" CI jobs rather than "check".
rust.doctest('rust-util-rs-doctests',
     _util_rs,
     dependencies: util_rs,
     suite: ['doc', 'rust']
)
