_migration_bindgen_args = []
c_bitfields = [
  'MigrationPolicy',
  'MigrationPriority',
  'VMStateFlags',
]
foreach enum : c_bitfields
  _migration_bindgen_args += ['--bitfield-enum', enum]
endforeach
#
# TODO: Remove this comment when the clang/libclang mismatch issue is solved.
#
# Rust bindings generation with `bindgen` might fail in some cases where the
# detected `libclang` does not match the expected `clang` version/target. In
# this case you must pass the path to `clang` and `libclang` to your build
# command invocation using the environment variables CLANG_PATH and
# LIBCLANG_PATH
_migration_bindings_inc_rs = rust.bindgen(
  input: 'wrapper.h',
  dependencies: common_ss.all_dependencies(),
  output: 'bindings.inc.rs',
  include_directories: bindings_incdir,
  bindgen_version: ['>=0.60.0'],
  args: bindgen_args_common + _migration_bindgen_args,
  c_args: bindgen_c_args,
)

_migration_rs = static_library(
  'migration',
  structured_sources(
    [
      'src/lib.rs',
      'src/bindings.rs',
      'src/migratable.rs',
      'src/vmstate.rs',
    ],
    {'.' : _migration_bindings_inc_rs},
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  link_with: [_util_rs, _bql_rs],
  dependencies: [common_rs, glib_sys_rs, qemu_macros],
)

migration_rs = declare_dependency(link_with: [_migration_rs],
  dependencies: [bql_rs, migration, qemuutil])

# Doctests are essentially integration tests, so they need the same dependencies.
# Note that running them requires the object files for C code, so place them
# in a separate suite that is run by the "build" CI jobs rather than "check".
rust.doctest('rust-migration-rs-doctests',
     _migration_rs,
     dependencies: migration_rs,
     suite: ['doc', 'rust'])
