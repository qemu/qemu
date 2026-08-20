_bits_rs = cargo_ws.package('bits').library()
cargo_ws.package('bits').override_dependency(declare_dependency(link_with: _bits_rs))

bits_rs = declare_dependency(link_with: _bits_rs)

rust.test('rust-bits-tests', _bits_rs,
          suite: ['unit', 'rust'])

rust.doctest('rust-bits-doctests', _bits_rs,
             dependencies: bits_rs,
             suite: ['doc', 'rust'])
