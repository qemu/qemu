// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use core::ptr::NonNull;
use std::os::raw::{c_uint, c_void};

use qemu_api::{bindings::*, zeroable::Zeroable};

use crate::device::PL011State;

pub static PL011_OPS: MemoryRegionOps = MemoryRegionOps {
    read: Some(pl011_read),
    write: Some(pl011_write),
    read_with_attrs: None,
    write_with_attrs: None,
    endianness: device_endian::DEVICE_NATIVE_ENDIAN,
    valid: Zeroable::ZERO,
    impl_: MemoryRegionOps__bindgen_ty_2 {
        min_access_size: 4,
        max_access_size: 4,
        ..Zeroable::ZERO
    },
};

unsafe extern "C" fn pl011_read(opaque: *mut c_void, addr: hwaddr, size: c_uint) -> u64 {
    let mut state = NonNull::new(opaque).unwrap().cast::<PL011State>();
    unsafe { state.as_mut() }.read(addr, size)
}

unsafe extern "C" fn pl011_write(opaque: *mut c_void, addr: hwaddr, data: u64, _size: c_uint) {
    let mut state = NonNull::new(opaque).unwrap().cast::<PL011State>();
    unsafe { state.as_mut() }.write(addr, data);
}
