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

use chardev::bindings::Chardev;
use common::Zeroable;
use glib_sys::{GArray, GByteArray, GHashTable, GHashTableIter, GList, GPtrArray, GSList, GString};
use migration::bindings::VMStateDescription;
use qom::bindings::ObjectClass;
use system::bindings::MemoryRegion;
use util::bindings::Error;

#[cfg(MESON)]
include!("bindings.inc.rs");

#[cfg(not(MESON))]
include!(concat!(env!("OUT_DIR"), "/bindings.inc.rs"));

unsafe impl Send for Property {}
unsafe impl Sync for Property {}

unsafe impl Send for TypeInfo {}
unsafe impl Sync for TypeInfo {}

unsafe impl Zeroable for Property__bindgen_ty_1 {}
unsafe impl Zeroable for Property {}
