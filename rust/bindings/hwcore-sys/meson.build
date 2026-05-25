_bindgen_hwcore_rs = rust.bindgen(
  args: bindgen_args_common + bindgen_args_data['hwcore-sys'].split(),
  kwargs: bindgen_kwargs)

_hwcore_sys_rs = cargo_ws.package('hwcore-sys').library(
  structured_sources(['lib.rs', _bindgen_hwcore_rs]))
cargo_ws.package('hwcore-sys').override_dependency(declare_dependency(link_with: _hwcore_sys_rs))

hwcore_sys_rs = declare_dependency(link_with: [_hwcore_sys_rs])
