// SPDX-License-Identifier: GPL-2.0-or-later
#![allow(
    dead_code,
    improper_ctypes_definitions,
    improper_ctypes,
    non_camel_case_types,
    non_snake_case,
    non_upper_case_globals,
    unsafe_op_in_unsafe_fn,
    clippy::missing_const_for_fn,
    clippy::too_many_arguments,
    clippy::approx_constant,
    clippy::use_self,
    clippy::useless_transmute,
    clippy::missing_safety_doc
)]

#[cfg(MESON)]
include!("bindings.inc.rs");

#[cfg(not(MESON))]
include!(concat!(env!("OUT_DIR"), "/bindings.inc.rs"));

unsafe impl Send for Property {}
unsafe impl Sync for Property {}
unsafe impl Sync for TypeInfo {}
unsafe impl Sync for VMStateDescription {}
unsafe impl Sync for VMStateField {}
unsafe impl Sync for VMStateInfo {}
