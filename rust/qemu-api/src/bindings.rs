// SPDX-License-Identifier: GPL-2.0-or-later
#![allow(
    dead_code,
    improper_ctypes_definitions,
    improper_ctypes,
    non_camel_case_types,
    non_snake_case,
    non_upper_case_globals,
    unsafe_op_in_unsafe_fn,
    clippy::pedantic,
    clippy::restriction,
    clippy::style,
    clippy::missing_const_for_fn,
    clippy::useless_transmute,
    clippy::missing_safety_doc
)]

//! `bindgen`-generated declarations.

#[cfg(MESON)]
include!("bindings.inc.rs");

#[cfg(not(MESON))]
include!(concat!(env!("OUT_DIR"), "/bindings.inc.rs"));

// SAFETY: these are implemented in C; the bindings need to assert that the
// BQL is taken, either directly or via `BqlCell` and `BqlRefCell`.
unsafe impl Send for BusState {}
unsafe impl Sync for BusState {}

unsafe impl Send for CharBackend {}
unsafe impl Sync for CharBackend {}

unsafe impl Send for Chardev {}
unsafe impl Sync for Chardev {}

unsafe impl Send for Clock {}
unsafe impl Sync for Clock {}

unsafe impl Send for DeviceState {}
unsafe impl Sync for DeviceState {}

unsafe impl Send for MemoryRegion {}
unsafe impl Sync for MemoryRegion {}

unsafe impl Send for ObjectClass {}
unsafe impl Sync for ObjectClass {}

unsafe impl Send for Object {}
unsafe impl Sync for Object {}

unsafe impl Send for SysBusDevice {}
unsafe impl Sync for SysBusDevice {}

// SAFETY: this is a pure data struct
unsafe impl Send for CoalescedMemoryRange {}
unsafe impl Sync for CoalescedMemoryRange {}

// SAFETY: these are constants and vtables; the Send and Sync requirements
// are deferred to the unsafe callbacks that they contain
unsafe impl Send for MemoryRegionOps {}
unsafe impl Sync for MemoryRegionOps {}

unsafe impl Send for Property {}
unsafe impl Sync for Property {}

unsafe impl Send for TypeInfo {}
unsafe impl Sync for TypeInfo {}

unsafe impl Send for VMStateDescription {}
unsafe impl Sync for VMStateDescription {}

unsafe impl Send for VMStateField {}
unsafe impl Sync for VMStateField {}

unsafe impl Send for VMStateInfo {}
unsafe impl Sync for VMStateInfo {}
