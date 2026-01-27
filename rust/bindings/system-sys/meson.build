_bindgen_system_rs = rust.bindgen(
  args: bindgen_args_common + bindgen_args_data['system-sys'].split(),
  kwargs: bindgen_kwargs)
_system_sys_rs = static_library(
  'system_sys',
  structured_sources(['lib.rs', _bindgen_system_rs]),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  dependencies: [common_rs, glib_sys_rs, hwcore_sys_rs, migration_sys_rs, qom_sys_rs, util_sys_rs],
)

system_sys_rs = declare_dependency(link_with: [_system_sys_rs])
