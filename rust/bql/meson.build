_bql_cfg = run_command(rustc_args,
  '--config-headers', config_host_h, '--features', files('Cargo.toml'),
  capture: true, check: true).stdout().strip().splitlines()

if get_option('debug_mutex')
  _bql_cfg += ['--cfg', 'feature="debug_cell"']
endif

_bql_rs = static_library(
  'bql',
  'src/lib.rs',
  rust_args: _bql_cfg,
  dependencies: [glib_sys_rs, util_sys_rs],
)

bql_rs = declare_dependency(link_with: [_bql_rs],
  dependencies: [qemuutil])

# Doctests are essentially integration tests, so they need the same dependencies.
# Note that running them requires the object files for C code, so place them
# in a separate suite that is run by the "build" CI jobs rather than "check".
rust.doctest('rust-bql-rs-doctests',
     _bql_rs,
     dependencies: bql_rs,
     suite: ['doc', 'rust'])
