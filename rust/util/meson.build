_util_rs = cargo_ws.package('util').library()
cargo_ws.package('util').override_dependency(declare_dependency(link_with: _util_rs))

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
