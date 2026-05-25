rust = import('rust')

lib_rs = configure_file(
  input: 'src/lib.rs',
  output: 'lib.rs',
  configuration: {
    'MESON_BUILD_ROOT': meson.project_build_root(),
  })

_trace_rs = cargo_ws.package('trace').library(
  structured_sources([lib_rs, trace_rs_targets]))
cargo_ws.package('trace').override_dependency(declare_dependency(link_with: _trace_rs))

trace_rs = declare_dependency(link_with: _trace_rs)
