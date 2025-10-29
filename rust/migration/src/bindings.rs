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
use glib_sys::{GHashTable, GHashTableIter, GPtrArray, GSList};

#[cfg(MESON)]
include!("bindings.inc.rs");

#[cfg(not(MESON))]
include!(concat!(env!("OUT_DIR"), "/bindings.inc.rs"));

unsafe impl Send for VMStateDescription {}
unsafe impl Sync for VMStateDescription {}

unsafe impl Send for VMStateField {}
unsafe impl Sync for VMStateField {}

unsafe impl Send for VMStateInfo {}
unsafe impl Sync for VMStateInfo {}

// bindgen does not derive Default here
#[allow(clippy::derivable_impls)]
impl Default for VMStateFlags {
    fn default() -> Self {
        Self(0)
    }
}

unsafe impl Zeroable for VMStateFlags {}
unsafe impl Zeroable for VMStateField {}
unsafe impl Zeroable for VMStateDescription {}
