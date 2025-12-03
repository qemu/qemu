_util_rs = static_library(
  'util',
  'src/lib.rs',
  dependencies: [anyhow_rs, libc_rs, foreign_rs, glib_sys_rs, common_rs, util_sys_rs],
)

util_rs = declare_dependency(link_with: [_util_rs], dependencies: [qemuutil, qom])

rust.test('rust-util-tests', _util_rs,
          dependencies: [qemuutil, qom],
          suite: ['unit', 'rust'])

# Doctests are essentially integration tests, so they need the same dependencies.
# Note that running them requires the object files for C code, so place them
# in a separate suite that is run by the "build" CI jobs rather than "check".
rust.doctest('rust-util-rs-doctests',
     _util_rs,
     dependencies: util_rs,
     suite: ['doc', 'rust']
)
