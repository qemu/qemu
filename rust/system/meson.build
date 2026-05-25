_system_rs = cargo_ws.package('system').library()
cargo_ws.package('system').override_dependency(declare_dependency(link_with: _system_rs))

system_rs = declare_dependency(link_with: [_system_rs],
  dependencies: [hwcore])
