project('unicode-ident-1-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '1.0.12',
  license: '(MIT OR Apache-2.0) AND Unicode-DFS-2016',
  default_options: [])

_unicode_ident_rs = static_library(
  'unicode_ident',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: ['--cap-lints', 'allow'],
  dependencies: [],
  native: true,
)

unicode_ident_dep = declare_dependency(
  link_with: _unicode_ident_rs,
)

meson.override_dependency('unicode-ident-1-rs', unicode_ident_dep, native: true)
