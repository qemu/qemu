// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use core::ptr::NonNull;

use qemu_api::{bindings::*, definitions::ObjectImpl};

use crate::device::PL011State;

#[used]
pub static VMSTATE_PL011: VMStateDescription = VMStateDescription {
    name: PL011State::TYPE_INFO.name,
    unmigratable: true,
    ..unsafe { ::core::mem::MaybeUninit::<VMStateDescription>::zeroed().assume_init() }
};

qemu_api::declare_properties! {
    PL011_PROPERTIES,
    qemu_api::define_property!(
        c"chardev",
        PL011State,
        char_backend,
        unsafe { &qdev_prop_chr },
        CharBackend
    ),
    qemu_api::define_property!(
        c"migrate-clk",
        PL011State,
        migrate_clock,
        unsafe { &qdev_prop_bool },
        bool
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
#[no_mangle]
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
#[no_mangle]
pub unsafe extern "C" fn pl011_reset(dev: *mut DeviceState) {
    unsafe {
        assert!(!dev.is_null());
        let mut state = NonNull::new_unchecked(dev.cast::<PL011State>());
        state.as_mut().reset();
    }
}
