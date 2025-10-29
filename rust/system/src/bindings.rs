// SPDX-License-Identifier: GPL-2.0-or-later
#![allow(
    dead_code,
    improper_ctypes_definitions,
    improper_ctypes,
    non_camel_case_types,
    non_snake_case,
    non_upper_case_globals,
    unnecessary_transmutes,
    unsafe_op_in_unsafe_fn,
    clippy::pedantic,
    clippy::restriction,
    clippy::style,
    clippy::missing_const_for_fn,
    clippy::ptr_offset_with_cast,
    clippy::useless_transmute,
    clippy::missing_safety_doc,
    clippy::too_many_arguments
)]

use common::Zeroable;
use glib_sys::{
    guint, GArray, GByteArray, GHashTable, GHashTableIter, GList, GPollFD, GPtrArray, GSList,
    GString,
};

#[cfg(MESON)]
include!("bindings.inc.rs");

#[cfg(not(MESON))]
include!(concat!(env!("OUT_DIR"), "/bindings.inc.rs"));

// SAFETY: these are constants and vtables; the Send and Sync requirements
// are deferred to the unsafe callbacks that they contain
unsafe impl Send for MemoryRegionOps {}
unsafe impl Sync for MemoryRegionOps {}

// SAFETY: this is a pure data struct
unsafe impl Send for CoalescedMemoryRange {}
unsafe impl Sync for CoalescedMemoryRange {}

unsafe impl Zeroable for MemoryRegionOps__bindgen_ty_1 {}
unsafe impl Zeroable for MemoryRegionOps__bindgen_ty_2 {}
unsafe impl Zeroable for MemoryRegionOps {}
unsafe impl Zeroable for MemTxAttrs {}
