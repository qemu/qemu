_bql_cfg = run_command(rustc_args,
  '--config-headers', config_host_h, '--features', files('Cargo.toml'),
  capture: true, check: true).stdout().strip().splitlines()

if get_option('debug_mutex')
  _bql_cfg += ['--cfg', 'feature="debug_cell"']
endif

#
# TODO: Remove this comment when the clang/libclang mismatch issue is solved.
#
# Rust bindings generation with `bindgen` might fail in some cases where the
# detected `libclang` does not match the expected `clang` version/target. In
# this case you must pass the path to `clang` and `libclang` to your build
# command invocation using the environment variables CLANG_PATH and
# LIBCLANG_PATH
_bql_bindings_inc_rs = rust.bindgen(
  input: 'wrapper.h',
  dependencies: common_ss.all_dependencies(),
  output: 'bindings.inc.rs',
  include_directories: bindings_incdir,
  bindgen_version: ['>=0.60.0'],
  args: bindgen_args_common,
  c_args: bindgen_c_args,
)

_bql_rs = static_library(
  'bql',
  structured_sources(
    [
      'src/lib.rs',
      'src/bindings.rs',
      'src/cell.rs',
    ],
    {'.': _bql_bindings_inc_rs}
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: _bql_cfg,
  dependencies: [glib_sys_rs],
)

bql_rs = declare_dependency(link_with: [_bql_rs],
  dependencies: [qemuutil])

# Doctests are essentially integration tests, so they need the same dependencies.
# Note that running them requires the object files for C code, so place them
# in a separate suite that is run by the "build" CI jobs rather than "check".
rust.doctest('rust-bql-rs-doctests',
     _bql_rs,
     dependencies: bql_rs,
     suite: ['doc', 'rust'])
