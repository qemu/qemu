_migration_rs = cargo_ws.package('migration').library()
cargo_ws.package('migration').override_dependency(declare_dependency(link_with: _migration_rs))

migration_rs = declare_dependency(link_with: [_migration_rs],
  dependencies: [bql_rs, migration, qemuutil])

# Doctests are essentially integration tests, so they need the same dependencies.
# Note that running them requires the object files for C code, so place them
# in a separate suite that is run by the "build" CI jobs rather than "check".
rust.doctest('rust-migration-rs-doctests',
     _migration_rs,
     dependencies: migration_rs,
     suite: ['doc', 'rust'])
