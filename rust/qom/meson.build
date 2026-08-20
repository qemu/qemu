_qom_rs = cargo_ws.package('qom').library()
cargo_ws.package('qom').override_dependency(declare_dependency(link_with: _qom_rs))

qom_rs = declare_dependency(link_with: [_qom_rs], dependencies: [qemu_macros, qom, qemuutil])

# Doctests are essentially integration tests, so they need the same dependencies.
# Note that running them requires the object files for C code, so place them
# in a separate suite that is run by the "build" CI jobs rather than "check".
rust.doctest('rust-qom-rs-doctests',
     _qom_rs,
     dependencies: qom_rs,
     suite: ['doc', 'rust'])
