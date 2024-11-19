// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use core::ptr::NonNull;
use std::os::raw::{c_int, c_void};

use qemu_api::{
    bindings::*, c_str, vmstate_clock, vmstate_fields, vmstate_int32, vmstate_subsections,
    vmstate_uint32, vmstate_uint32_array, vmstate_unused, zeroable::Zeroable,
};

use crate::device::{PL011State, PL011_FIFO_DEPTH};

extern "C" fn pl011_clock_needed(opaque: *mut c_void) -> bool {
    unsafe {
        debug_assert!(!opaque.is_null());
        let state = NonNull::new_unchecked(opaque.cast::<PL011State>());
        state.as_ref().migrate_clock
    }
}

/// Migration subsection for [`PL011State`] clock.
pub static VMSTATE_PL011_CLOCK: VMStateDescription = VMStateDescription {
    name: c_str!("pl011/clock").as_ptr(),
    version_id: 1,
    minimum_version_id: 1,
    needed: Some(pl011_clock_needed),
    fields: vmstate_fields! {
        vmstate_clock!(clock, PL011State),
    },
    ..Zeroable::ZERO
};

extern "C" fn pl011_post_load(opaque: *mut c_void, version_id: c_int) -> c_int {
    unsafe {
        debug_assert!(!opaque.is_null());
        let mut state = NonNull::new_unchecked(opaque.cast::<PL011State>());
        let result = state.as_mut().post_load(version_id as u32);
        if result.is_err() {
            -1
        } else {
            0
        }
    }
}

pub static VMSTATE_PL011: VMStateDescription = VMStateDescription {
    name: c_str!("pl011").as_ptr(),
    version_id: 2,
    minimum_version_id: 2,
    post_load: Some(pl011_post_load),
    fields: vmstate_fields! {
        vmstate_unused!(core::mem::size_of::<u32>()),
        vmstate_uint32!(flags, PL011State),
        vmstate_uint32!(line_control, PL011State),
        vmstate_uint32!(receive_status_error_clear, PL011State),
        vmstate_uint32!(control, PL011State),
        vmstate_uint32!(dmacr, PL011State),
        vmstate_uint32!(int_enabled, PL011State),
        vmstate_uint32!(int_level, PL011State),
        vmstate_uint32_array!(read_fifo, PL011State, PL011_FIFO_DEPTH),
        vmstate_uint32!(ilpr, PL011State),
        vmstate_uint32!(ibrd, PL011State),
        vmstate_uint32!(fbrd, PL011State),
        vmstate_uint32!(ifl, PL011State),
        vmstate_int32!(read_pos, PL011State),
        vmstate_int32!(read_count, PL011State),
        vmstate_int32!(read_trigger, PL011State),
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

qemu_api::device_class_init! {
    pl011_class_init,
    props => PL011_PROPERTIES,
    realize_fn => Some(pl011_realize),
    legacy_reset_fn => Some(pl011_reset),
    vmsd => VMSTATE_PL011,
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer, that has
/// the same size as [`PL011State`]. We also expect the device is
/// readable/writeable from one thread at any time.
pub unsafe extern "C" fn pl011_realize(dev: *mut DeviceState, _errp: *mut *mut Error) {
    unsafe {
        assert!(!dev.is_null());
        let mut state = NonNull::new_unchecked(dev.cast::<PL011State>());
        state.as_mut().realize();
    }
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer, that has
/// the same size as [`PL011State`]. We also expect the device is
/// readable/writeable from one thread at any time.
pub unsafe extern "C" fn pl011_reset(dev: *mut DeviceState) {
    unsafe {
        assert!(!dev.is_null());
        let mut state = NonNull::new_unchecked(dev.cast::<PL011State>());
        state.as_mut().reset();
    }
}
