// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

//! PL011 QEMU Device Model
//!
//! This library implements a device model for the PrimeCell® UART (PL011)
//! device in QEMU.
//!
//! # Library crate
//!
//! See [`PL011State`](crate::device::PL011State) for the device model type and
//! the [`registers`] module for register types.

use qemu_api::c_str;

mod device;
mod device_class;
mod registers;

pub use device::pl011_create;

pub const TYPE_PL011: &::std::ffi::CStr = c_str!("pl011");
pub const TYPE_PL011_LUMINARY: &::std::ffi::CStr = c_str!("pl011_luminary");
