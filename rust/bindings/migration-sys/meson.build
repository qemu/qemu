_bindgen_migration_rs = rust.bindgen(
  args: bindgen_args_common + bindgen_args_data['migration-sys'].split(),
  kwargs: bindgen_kwargs)
_migration_sys_rs = static_library(
  'migration_sys',
  structured_sources(['lib.rs', _bindgen_migration_rs]),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  dependencies: [glib_sys_rs, common_rs, util_sys_rs],
)

migration_sys_rs = declare_dependency(link_with: [_migration_sys_rs])
