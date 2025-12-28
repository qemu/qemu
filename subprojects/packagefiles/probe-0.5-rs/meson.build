project('probe-0.5-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '0.5.2',
  license: 'Apache-2.0 OR MIT',
  default_options: [])

_probe_rs = static_library(
  'probe',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: [
    '--cap-lints', 'allow',
  ],
)

probe_deps = declare_dependency(
  link_with: _probe_rs,
)

meson.override_dependency('probe-0.5-rs', probe_deps)
