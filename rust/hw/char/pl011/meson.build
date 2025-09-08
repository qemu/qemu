_libpl011_rs = static_library(
  'pl011',
  files('src/lib.rs'),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  dependencies: [
    bilge_rs,
    bilge_impl_rs,
    bits_rs,
    common_rs,
    qemu_api_rs,
    util_rs,
    migration_rs,
    bql_rs,
    qemu_macros,
    qom_rs,
    chardev_rs,
    system_rs,
    hwcore_rs,
  ],
)

rust_devices_ss.add(when: 'CONFIG_X_PL011_RUST', if_true: [declare_dependency(
  link_whole: [_libpl011_rs],
  # Putting proc macro crates in `dependencies` is necessary for Meson to find
  # them when compiling the root per-target static rust lib.
  dependencies: [bilge_impl_rs, qemu_macros],
  variables: {'crate': 'pl011'},
)])
