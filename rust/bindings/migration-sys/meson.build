_bindgen_migration_rs = rust.bindgen(
  args: bindgen_args_common + bindgen_args_data['migration-sys'].split(),
  kwargs: bindgen_kwargs)

_migration_sys_rs = cargo_ws.package('migration-sys').library(
  structured_sources(['lib.rs', _bindgen_migration_rs]))
cargo_ws.package('migration-sys').override_dependency(declare_dependency(link_with: _migration_sys_rs))

migration_sys_rs = declare_dependency(link_with: [_migration_sys_rs])
