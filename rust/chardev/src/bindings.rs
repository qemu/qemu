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
    gboolean, guint, GArray, GHashTable, GHashTableIter, GIOCondition, GMainContext, GPollFD,
    GPtrArray, GSList, GSource, GSourceFunc,
};

#[cfg(MESON)]
include!("bindings.inc.rs");

#[cfg(not(MESON))]
include!(concat!(env!("OUT_DIR"), "/bindings.inc.rs"));

// SAFETY: these are implemented in C; the bindings need to assert that the
// BQL is taken, either directly or via `BqlCell` and `BqlRefCell`.
// When bindings for character devices are introduced, this can be
// moved to the Opaque<> wrapper in src/chardev.rs.
unsafe impl Send for CharFrontend {}
unsafe impl Sync for CharFrontend {}

unsafe impl Zeroable for CharFrontend {}
