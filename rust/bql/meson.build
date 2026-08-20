_bql_cfg = []
if get_option('debug_mutex')
  _bql_cfg += ['--cfg', 'feature="debug_cell"']
endif

_bql_rs = cargo_ws.package('bql').library(rust_args: _bql_cfg)
cargo_ws.package('bql').override_dependency(declare_dependency(link_with: _bql_rs))

bql_rs = declare_dependency(link_with: [_bql_rs],
  dependencies: [qemuutil])

# Doctests are essentially integration tests, so they need the same dependencies.
# Note that running them requires the object files for C code, so place them
# in a separate suite that is run by the "build" CI jobs rather than "check".
rust.doctest('rust-bql-rs-doctests',
     _bql_rs,
     dependencies: bql_rs,
     suite: ['doc', 'rust'])
