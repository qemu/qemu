project('either-1-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '1.12.0',
  license: 'MIT OR Apache-2.0',
  default_options: [])

_either_rs = static_library(
  'either',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options: ['rust_std=2018', 'build.rust_std=2018'],
  rust_abi: 'rust',
  rust_args: [
    '--cap-lints', 'allow',
    '--cfg', 'feature="use_std"',
    '--cfg', 'feature="use_alloc"',
  ],
  dependencies: [],
  native: true,
)

either_dep = declare_dependency(
  link_with: _either_rs,
)

meson.override_dependency('either-1-rs', either_dep, native: true)
