// Copyright (C) 2024 Intel Corporation.
// Author(s): Zhao Liu <zhai1.liu@intel.com>
// SPDX-License-Identifier: GPL-2.0-or-later

//! # HPET QEMU Device Model
//!
//! This library implements a device model for the IA-PC HPET (High
//! Precision Event Timers) device in QEMU.

use qemu_api::c_str;

pub mod fw_cfg;
pub mod hpet;

pub const TYPE_HPET: &::std::ffi::CStr = c_str!("hpet");
