# QEMU bindings and API wrappers

This library exports helper Rust types, Rust macros and C FFI bindings for internal QEMU APIs.

The C bindings can be generated with `bindgen`, using this build target:

```console
$ ninja bindings.inc.rs
```

## Generate Rust documentation

To generate docs for this crate, including private items:

```sh
pyvenv/bin/meson devenv -w ../rust cargo doc --no-deps --document-private-items
```
