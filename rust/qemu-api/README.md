# QEMU bindings and API wrappers

This library exports helper Rust types, Rust macros and C FFI bindings for internal QEMU APIs.

The C bindings can be generated with `bindgen`, using this build target:

```console
$ make bindings.inc.rs
```

## Generate Rust documentation

Common Cargo tasks can be performed from the QEMU build directory

```console
$ make clippy
$ make rustfmt
$ make rustdoc
```
