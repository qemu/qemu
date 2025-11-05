c_enums = [
  'QEMUChrEvent',
]
_chardev_bindgen_args = []
foreach enum : c_enums
  _chardev_bindgen_args += ['--rustified-enum', enum]
endforeach

# TODO: Remove this comment when the clang/libclang mismatch issue is solved.
#
# Rust bindings generation with `bindgen` might fail in some cases where the
# detected `libclang` does not match the expected `clang` version/target. In
# this case you must pass the path to `clang` and `libclang` to your build
# command invocation using the environment variables CLANG_PATH and
# LIBCLANG_PATH
_chardev_bindings_inc_rs = rust.bindgen(
  input: 'wrapper.h',
  dependencies: common_ss.all_dependencies(),
  output: 'bindings.inc.rs',
  include_directories: bindings_incdir,
  bindgen_version: ['>=0.60.0'],
  args: bindgen_args_common + _chardev_bindgen_args,
  c_args: bindgen_c_args,
)

_chardev_rs = static_library(
  'chardev',
  structured_sources(
    [
      'src/lib.rs',
      'src/bindings.rs',
      'src/chardev.rs',
    ],
    {'.': _chardev_bindings_inc_rs}
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  link_with: [_bql_rs, _migration_rs, _qom_rs, _util_rs],
  dependencies: [glib_sys_rs, common_rs, qemu_macros],
)

chardev_rs = declare_dependency(link_with: [_chardev_rs], dependencies: [chardev])
