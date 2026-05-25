_bindgen_system_rs = rust.bindgen(
  args: bindgen_args_common + bindgen_args_data['system-sys'].split(),
  kwargs: bindgen_kwargs)

_system_sys_rs = cargo_ws.package('system-sys').library(
  structured_sources(['lib.rs', _bindgen_system_rs]))
cargo_ws.package('system-sys').override_dependency(declare_dependency(link_with: _system_sys_rs))

system_sys_rs = declare_dependency(link_with: [_system_sys_rs])
