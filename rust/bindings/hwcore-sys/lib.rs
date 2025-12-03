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

use chardev_sys::Chardev;
use common::Zeroable;
use glib_sys::GSList;
use migration_sys::VMStateDescription;
use qom_sys::{
    InterfaceClass, Object, ObjectClass, ObjectProperty, ObjectPropertyAccessor,
    ObjectPropertyRelease,
};
use util_sys::{Error, QDict, QList};

#[cfg(MESON)]
include!("bindings.inc.rs");

#[cfg(not(MESON))]
include!(concat!(env!("OUT_DIR"), "/bindings.inc.rs"));

unsafe impl Send for Property {}
unsafe impl Sync for Property {}

unsafe impl Zeroable for Property__bindgen_ty_1 {}
unsafe impl Zeroable for Property {}
