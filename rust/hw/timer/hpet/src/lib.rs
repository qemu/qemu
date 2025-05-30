// Copyright (C) 2024 Intel Corporation.
// Author(s): Zhao Liu <zhao1.liu@intel.com>
// SPDX-License-Identifier: GPL-2.0-or-later

//! # HPET QEMU Device Model
//!
//! This library implements a device model for the IA-PC HPET (High
//! Precision Event Timers) device in QEMU.

pub mod device;
pub mod fw_cfg;

pub const TYPE_HPET: &::std::ffi::CStr = c"hpet";
