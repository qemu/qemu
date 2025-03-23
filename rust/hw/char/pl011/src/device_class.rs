// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::{
    os::raw::{c_int, c_void},
    ptr::NonNull,
};

use qemu_api::{
    bindings::{qdev_prop_bool, qdev_prop_chr},
    c_str,
    prelude::*,
    vmstate::VMStateDescription,
    vmstate_clock, vmstate_fields, vmstate_of, vmstate_struct, vmstate_subsections, vmstate_unused,
    zeroable::Zeroable,
};

use crate::device::{PL011Registers, PL011State};

extern "C" fn pl011_clock_needed(opaque: *mut c_void) -> bool {
    let state = NonNull::new(opaque).unwrap().cast::<PL011State>();
    unsafe { state.as_ref().migrate_clock }
}

/// Migration subsection for [`PL011State`] clock.
static VMSTATE_PL011_CLOCK: VMStateDescription = VMStateDescription {
    name: c_str!("pl011/clock").as_ptr(),
    version_id: 1,
    minimum_version_id: 1,
    needed: Some(pl011_clock_needed),
    fields: vmstate_fields! {
        vmstate_clock!(PL011State, clock),
    },
    ..Zeroable::ZERO
};

extern "C" fn pl011_post_load(opaque: *mut c_void, version_id: c_int) -> c_int {
    let state = NonNull::new(opaque).unwrap().cast::<PL011State>();
    let result = unsafe { state.as_ref().post_load(version_id as u32) };
    if result.is_err() {
        -1
    } else {
        0
    }
}

static VMSTATE_PL011_REGS: VMStateDescription = VMStateDescription {
    name: c_str!("pl011/regs").as_ptr(),
    version_id: 2,
    minimum_version_id: 2,
    fields: vmstate_fields! {
        vmstate_of!(PL011Registers, flags),
        vmstate_of!(PL011Registers, line_control),
        vmstate_of!(PL011Registers, receive_status_error_clear),
        vmstate_of!(PL011Registers, control),
        vmstate_of!(PL011Registers, dmacr),
        vmstate_of!(PL011Registers, int_enabled),
        vmstate_of!(PL011Registers, int_level),
        vmstate_of!(PL011Registers, read_fifo),
        vmstate_of!(PL011Registers, ilpr),
        vmstate_of!(PL011Registers, ibrd),
        vmstate_of!(PL011Registers, fbrd),
        vmstate_of!(PL011Registers, ifl),
        vmstate_of!(PL011Registers, read_pos),
        vmstate_of!(PL011Registers, read_count),
        vmstate_of!(PL011Registers, read_trigger),
    },
    ..Zeroable::ZERO
};

pub static VMSTATE_PL011: VMStateDescription = VMStateDescription {
    name: c_str!("pl011").as_ptr(),
    version_id: 2,
    minimum_version_id: 2,
    post_load: Some(pl011_post_load),
    fields: vmstate_fields! {
        vmstate_unused!(core::mem::size_of::<u32>()),
        vmstate_struct!(PL011State, regs, &VMSTATE_PL011_REGS, BqlRefCell<PL011Registers>),
    },
    subsections: vmstate_subsections! {
        VMSTATE_PL011_CLOCK
    },
    ..Zeroable::ZERO
};

qemu_api::declare_properties! {
    PL011_PROPERTIES,
    qemu_api::define_property!(
        c_str!("chardev"),
        PL011State,
        char_backend,
        unsafe { &qdev_prop_chr },
        CharBackend
    ),
    qemu_api::define_property!(
        c_str!("migrate-clk"),
        PL011State,
        migrate_clock,
        unsafe { &qdev_prop_bool },
        bool,
        default = true
    ),
}
