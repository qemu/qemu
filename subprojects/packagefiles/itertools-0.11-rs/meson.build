project('itertools-0.11-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '0.11.0',
  license: 'MIT OR Apache-2.0',
  default_options: [])

subproject('either-1-rs', required: true)

either_dep = dependency('either-1-rs', native: true)

_itertools_rs = static_library(
  'itertools',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options: ['rust_std=2018', 'build.rust_std=2018'],
  rust_abi: 'rust',
  rust_args: [
    '--cap-lints', 'allow',
    '--cfg', 'feature="use_std"',
    '--cfg', 'feature="use_alloc"',
  ],
  dependencies: [
    either_dep,
  ],
  native: true,
)

itertools_dep = declare_dependency(
  link_with: _itertools_rs,
)

meson.override_dependency('itertools-0.11-rs', itertools_dep, native: true)
