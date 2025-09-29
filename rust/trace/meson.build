rust = import('rust')

lib_rs = configure_file(
  input: 'src/lib.rs',
  output: 'lib.rs',
  configuration: {
    'MESON_BUILD_ROOT': meson.project_build_root(),
  })

_trace_rs = static_library(
  'trace',             # Library name,
  lib_rs,
  trace_rs_targets,         # List of generated `.rs` custom targets
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  dependencies: [libc_rs],
  rust_abi: 'rust',
)

trace_rs = declare_dependency(link_with: _trace_rs)
