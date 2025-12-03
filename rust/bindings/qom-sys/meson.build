_bindgen_qom_rs = rust.bindgen(
  args: bindgen_args_common + bindgen_args_data['qom-sys'].split(),
  kwargs: bindgen_kwargs)
_qom_sys_rs = static_library(
  'qom_sys',
  structured_sources(['lib.rs', _bindgen_qom_rs]),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  dependencies: [glib_sys_rs, util_sys_rs],
)

qom_sys_rs = declare_dependency(link_with: [_qom_sys_rs])
