_bindgen_chardev_rs = rust.bindgen(
  args: bindgen_args_common + bindgen_args_data['chardev-sys'].split(),
  kwargs: bindgen_kwargs)

_chardev_sys_rs = cargo_ws.package('chardev-sys').library(
  structured_sources(['lib.rs', _bindgen_chardev_rs]))
cargo_ws.package('chardev-sys').override_dependency(declare_dependency(link_with: _chardev_sys_rs))

chardev_sys_rs = declare_dependency(link_with: [_chardev_sys_rs])
