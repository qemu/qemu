_bits_rs = static_library(
  'bits',
  'src/lib.rs',
  dependencies: [qemu_macros],
)

bits_rs = declare_dependency(link_with: _bits_rs)

rust.test('rust-bits-tests', _bits_rs,
          suite: ['unit', 'rust'])

rust.doctest('rust-bits-doctests', _bits_rs,
             dependencies: bits_rs,
             suite: ['doc', 'rust'])
