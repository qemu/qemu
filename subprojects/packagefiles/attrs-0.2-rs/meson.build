project('attrs-0.2-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '0.2.9',
  license: 'MIT OR Apache-2.0',
  default_options: [])

subproject('proc-macro2-1-rs', required: true)
subproject('syn-2-rs', required: true)

proc_macro2_dep = dependency('proc-macro2-1-rs', native: true)
syn_dep = dependency('syn-2-rs', native: true)

_attrs_rs = static_library(
  'attrs',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: [
    '--cap-lints', 'allow',
  ],
  dependencies: [
    proc_macro2_dep,
    syn_dep,
  ],
  native: true,
)

attrs_dep = declare_dependency(
  link_with: _attrs_rs,
)

meson.override_dependency('attrs-0.2-rs', attrs_dep, native: true)
