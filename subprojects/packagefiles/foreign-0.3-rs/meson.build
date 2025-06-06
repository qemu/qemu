project('foreign-0.3-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '0.2.0',
  license: 'MIT OR Apache-2.0',
  default_options: [])

subproject('libc-0.2-rs', required: true)
libc_rs = dependency('libc-0.2-rs')

_foreign_rs = static_library(
  'foreign',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: [
    '--cap-lints', 'allow',
  ],
  dependencies: [libc_rs],
)

foreign_dep = declare_dependency(
  link_with: _foreign_rs,
)

meson.override_dependency('foreign-0.3-rs', foreign_dep)
