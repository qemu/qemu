_libhpet_rs = static_library(
  'hpet',
  files('src/lib.rs'),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  dependencies: [
    common_rs,
    util_rs,
    migration_rs,
    bql_rs,
    qom_rs,
    system_rs,
    hwcore_rs,
  ],
)

rust_devices_ss.add(when: 'CONFIG_X_HPET_RUST', if_true: [declare_dependency(
  link_whole: [_libhpet_rs],
  variables: {'crate': 'hpet'},
)])
