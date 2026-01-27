_chardev_rs = static_library(
  'chardev',
  'src/lib.rs',
  link_with: [_bql_rs, _migration_rs, _qom_rs, _util_rs],
  dependencies: [glib_sys_rs, common_rs, qemu_macros, chardev_sys_rs],
)

chardev_rs = declare_dependency(link_with: [_chardev_rs], dependencies: [chardev, qemuutil])
