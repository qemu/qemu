_migration_rs = static_library(
  'migration',
  'src/lib.rs',
  link_with: [_util_rs, _bql_rs],
  dependencies: [common_rs, glib_sys_rs, qemu_macros, migration_sys_rs],
)

migration_rs = declare_dependency(link_with: [_migration_rs],
  dependencies: [bql_rs, migration, qemuutil])

# Doctests are essentially integration tests, so they need the same dependencies.
# Note that running them requires the object files for C code, so place them
# in a separate suite that is run by the "build" CI jobs rather than "check".
rust.doctest('rust-migration-rs-doctests',
     _migration_rs,
     dependencies: migration_rs,
     suite: ['doc', 'rust'])
