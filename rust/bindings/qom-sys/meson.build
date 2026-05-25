_bindgen_qom_rs = rust.bindgen(
  args: bindgen_args_common + bindgen_args_data['qom-sys'].split(),
  kwargs: bindgen_kwargs)

_qom_sys_rs = cargo_ws.package('qom-sys').library(
  structured_sources(['lib.rs', _bindgen_qom_rs]))
cargo_ws.package('qom-sys').override_dependency(declare_dependency(link_with: _qom_sys_rs))

qom_sys_rs = declare_dependency(link_with: [_qom_sys_rs])
