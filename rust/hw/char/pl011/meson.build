_libpl011_bindings_inc_rs = rust.bindgen(
  args: bindgen_args_common + [
    '--allowlist-file', meson.project_source_root() / 'include/hw/char/pl011.h',
    '--blocklist-file',
      meson.project_source_root() /
      'include/(block\|chardev/|exec/|hw/core/|qemu/|qom/|system/).*',
  ],
  kwargs: bindgen_kwargs,
)

_libpl011_rs = cargo_ws.package('pl011').library(
  structured_sources(
    [
      'src/lib.rs',
      'src/bindings.rs',
      'src/device.rs',
      'src/registers.rs',
    ],
    {'.' : _libpl011_bindings_inc_rs},
  ),
)

rust_devices_ss.add(when: 'CONFIG_X_PL011_RUST', if_true: [declare_dependency(
  link_whole: [_libpl011_rs],
  variables: {'crate': 'pl011'},
)])
