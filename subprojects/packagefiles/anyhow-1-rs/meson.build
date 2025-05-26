project('anyhow-1-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '1.0.98',
  license: 'MIT OR Apache-2.0',
  default_options: [])

rustc = meson.get_compiler('rust')

rust_args = ['--cap-lints', 'allow']
rust_args += ['--cfg', 'feature="std"']
if rustc.version().version_compare('<1.65.0')
  error('rustc version ' + rustc.version() + ' is unsupported. Please upgrade to at least 1.65.0')
endif
rust_args += [ '--cfg', 'std_backtrace' ] # >= 1.65.0
if rustc.version().version_compare('<1.81.0')
  rust_args += [ '--cfg', 'anyhow_no_core_error' ]
endif

_anyhow_rs = static_library(
  'anyhow',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options: ['rust_std=2018', 'build.rust_std=2018'],
  rust_abi: 'rust',
  rust_args: rust_args,
  dependencies: [],
)

anyhow_dep = declare_dependency(
  link_with: _anyhow_rs,
)

meson.override_dependency('anyhow-1-rs', anyhow_dep)
