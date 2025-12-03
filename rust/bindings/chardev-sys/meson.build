_bindgen_chardev_rs = rust.bindgen(
  args: bindgen_args_common + bindgen_args_data['chardev-sys'].split(),
  kwargs: bindgen_kwargs)
_chardev_sys_rs = static_library(
  'chardev_sys',
  structured_sources(['lib.rs', _bindgen_chardev_rs]),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  dependencies: [glib_sys_rs, common_rs, qom_sys_rs, util_sys_rs],
)

chardev_sys_rs = declare_dependency(link_with: [_chardev_sys_rs])
