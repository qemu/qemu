_libhpet_rs = static_library(
  'hpet',
  files('src/lib.rs'),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  dependencies: [
    qemu_api,
    qemu_api_macros,
  ],
)

rust_devices_ss.add(when: 'CONFIG_X_HPET_RUST', if_true: [declare_dependency(
  link_whole: [_libhpet_rs],
  # Putting proc macro crates in `dependencies` is necessary for Meson to find
  # them when compiling the root per-target static rust lib.
  dependencies: [qemu_api_macros],
  variables: {'crate': 'hpet'},
)])
