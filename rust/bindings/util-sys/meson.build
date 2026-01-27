_bindgen_util_rs = rust.bindgen(
  args: bindgen_args_common + bindgen_args_data['util-sys'].split(),
  kwargs: bindgen_kwargs)
_util_sys_rs = static_library(
  'util_sys',
  structured_sources(['lib.rs', _bindgen_util_rs]),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  dependencies: [glib_sys_rs],
)

util_sys_rs = declare_dependency(link_with: [_util_sys_rs])
