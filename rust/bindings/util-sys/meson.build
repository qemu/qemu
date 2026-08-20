_bindgen_util_rs = rust.bindgen(
  args: bindgen_args_common + bindgen_args_data['util-sys'].split(),
  kwargs: bindgen_kwargs)

_util_sys_rs = cargo_ws.package('util-sys').library(
  structured_sources(['lib.rs', _bindgen_util_rs]))
cargo_ws.package('util-sys').override_dependency(declare_dependency(link_with: _util_sys_rs))

util_sys_rs = declare_dependency(link_with: [_util_sys_rs])
