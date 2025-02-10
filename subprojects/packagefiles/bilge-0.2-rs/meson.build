project(
  'bilge-0.2-rs',
  'rust',
  meson_version: '>=1.5.0',
  version : '0.2.0',
  license : 'MIT or Apache-2.0',
)

subproject('arbitrary-int-1-rs', required: true)
subproject('bilge-impl-0.2-rs', required: true)

arbitrary_int_dep = dependency('arbitrary-int-1-rs')
bilge_impl_dep = dependency('bilge-impl-0.2-rs')

lib = static_library(
  'bilge',
  'src/lib.rs',
  override_options : ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi : 'rust',
  rust_args: ['--cap-lints', 'allow'],
  dependencies: [
    arbitrary_int_dep,
    bilge_impl_dep,
  ],
)

bilge_dep = declare_dependency(
  link_with : [lib],
)

meson.override_dependency('bilge-0.2-rs', bilge_dep)
