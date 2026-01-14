_system_rs = static_library(
  'system',
  'src/lib.rs',
  link_with: [_bql_rs, _hwcore_rs, _migration_rs, _qom_rs, _util_rs],
  dependencies: [glib_sys_rs, common_rs, qemu_macros, system_sys_rs],
)

system_rs = declare_dependency(link_with: [_system_rs],
  dependencies: [hwcore])
