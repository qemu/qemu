_common_cfg = run_command(rustc_args,
  '--config-headers', config_host_h, '--features', files('Cargo.toml'),
  capture: true, check: true).stdout().strip().splitlines()

_common_rs = static_library(
  'common',
  'src/lib.rs',
  rust_args: _common_cfg,
  dependencies: [libc_rs, qemu_macros],
)

common_rs = declare_dependency(link_with: [_common_rs])

rust.test('rust-common-tests', _common_rs,
          suite: ['unit', 'rust'])

# Doctests are essentially integration tests, so they need the same dependencies.
# Note that running them requires the object files for C code, so place them
# in a separate suite that is run by the "build" CI jobs rather than "check".
rust.doctest('rust-common-doctests',
     _common_rs,
     dependencies: common_rs,
     suite: ['doc', 'rust'])
