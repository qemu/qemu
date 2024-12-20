project('arbitrary-int-1-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '1.2.7',
  license: 'MIT',
  default_options: [])

_arbitrary_int_rs = static_library(
  'arbitrary_int',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_args: ['--cap-lints', 'allow'],
  rust_abi: 'rust',
  dependencies: [],
)

arbitrary_int_dep = declare_dependency(
  link_with: _arbitrary_int_rs,
)

meson.override_dependency('arbitrary-int-1-rs', arbitrary_int_dep)
