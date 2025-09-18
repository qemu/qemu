// Copyright 2024 Red Hat, Inc.
// Author(s): Paolo Bonzini <pbonzini@redhat.com>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Bindings for `MemoryRegion`, `MemoryRegionOps` and `MemTxAttrs`

use std::{
    ffi::{c_uint, c_void, CStr, CString},
    marker::PhantomData,
};

use common::{callbacks::FnCall, uninit::MaybeUninitField, zeroable::Zeroable, Opaque};
use qom::prelude::*;

use crate::bindings::{self, device_endian, memory_region_init_io};
pub use crate::bindings::{hwaddr, MemTxAttrs};

pub struct MemoryRegionOps<T>(
    bindings::MemoryRegionOps,
    // Note: quite often you'll see PhantomData<fn(&T)> mentioned when discussing
    // covariance and contravariance; you don't need any of those to understand
    // this usage of PhantomData.  Quite simply, MemoryRegionOps<T> *logically*
    // holds callbacks that take an argument of type &T, except the type is erased
    // before the callback is stored in the bindings::MemoryRegionOps field.
    // The argument of PhantomData is a function pointer in order to represent
    // that relationship; while that will also provide desirable and safe variance
    // for T, variance is not the point but just a consequence.
    PhantomData<fn(&T)>,
);

// SAFETY: When a *const T is passed to the callbacks, the call itself
// is done in a thread-safe manner.  The invocation is okay as long as
// T itself is `Sync`.
unsafe impl<T: Sync> Sync for MemoryRegionOps<T> {}

#[derive(Clone)]
pub struct MemoryRegionOpsBuilder<T>(bindings::MemoryRegionOps, PhantomData<fn(&T)>);

unsafe extern "C" fn memory_region_ops_read_cb<T, F: for<'a> FnCall<(&'a T, hwaddr, u32), u64>>(
    opaque: *mut c_void,
    addr: hwaddr,
    size: c_uint,
) -> u64 {
    F::call((unsafe { &*(opaque.cast::<T>()) }, addr, size))
}

unsafe extern "C" fn memory_region_ops_write_cb<T, F: for<'a> FnCall<(&'a T, hwaddr, u64, u32)>>(
    opaque: *mut c_void,
    addr: hwaddr,
    data: u64,
    size: c_uint,
) {
    F::call((unsafe { &*(opaque.cast::<T>()) }, addr, data, size))
}

impl<T> MemoryRegionOpsBuilder<T> {
    #[must_use]
    pub const fn read<F: for<'a> FnCall<(&'a T, hwaddr, u32), u64>>(mut self, _f: &F) -> Self {
        self.0.read = Some(memory_region_ops_read_cb::<T, F>);
        self
    }

    #[must_use]
    pub const fn write<F: for<'a> FnCall<(&'a T, hwaddr, u64, u32)>>(mut self, _f: &F) -> Self {
        self.0.write = Some(memory_region_ops_write_cb::<T, F>);
        self
    }

    #[must_use]
    pub const fn big_endian(mut self) -> Self {
        self.0.endianness = device_endian::DEVICE_BIG_ENDIAN;
        self
    }

    #[must_use]
    pub const fn little_endian(mut self) -> Self {
        self.0.endianness = device_endian::DEVICE_LITTLE_ENDIAN;
        self
    }

    #[must_use]
    pub const fn native_endian(mut self) -> Self {
        self.0.endianness = device_endian::DEVICE_NATIVE_ENDIAN;
        self
    }

    #[must_use]
    pub const fn valid_sizes(mut self, min: u32, max: u32) -> Self {
        self.0.valid.min_access_size = min;
        self.0.valid.max_access_size = max;
        self
    }

    #[must_use]
    pub const fn valid_unaligned(mut self) -> Self {
        self.0.valid.unaligned = true;
        self
    }

    #[must_use]
    pub const fn impl_sizes(mut self, min: u32, max: u32) -> Self {
        self.0.impl_.min_access_size = min;
        self.0.impl_.max_access_size = max;
        self
    }

    #[must_use]
    pub const fn impl_unaligned(mut self) -> Self {
        self.0.impl_.unaligned = true;
        self
    }

    #[must_use]
    pub const fn build(self) -> MemoryRegionOps<T> {
        MemoryRegionOps::<T>(self.0, PhantomData)
    }

    #[must_use]
    pub const fn new() -> Self {
        Self(bindings::MemoryRegionOps::ZERO, PhantomData)
    }
}

impl<T> Default for MemoryRegionOpsBuilder<T> {
    fn default() -> Self {
        Self::new()
    }
}

/// A safe wrapper around [`bindings::MemoryRegion`].
#[repr(transparent)]
#[derive(common::Wrapper)]
pub struct MemoryRegion(Opaque<bindings::MemoryRegion>);

unsafe impl Send for MemoryRegion {}
unsafe impl Sync for MemoryRegion {}

impl MemoryRegion {
    unsafe fn do_init_io(
        slot: *mut bindings::MemoryRegion,
        owner: *mut bindings::Object,
        ops: &'static bindings::MemoryRegionOps,
        name: &'static str,
        size: u64,
    ) {
        unsafe {
            let cstr = CString::new(name).unwrap();
            memory_region_init_io(
                slot,
                owner,
                ops,
                owner.cast::<c_void>(),
                cstr.as_ptr(),
                size,
            );
        }
    }

    pub fn init_io<T: IsA<Object>>(
        this: &mut MaybeUninitField<'_, T, Self>,
        ops: &'static MemoryRegionOps<T>,
        name: &'static str,
        size: u64,
    ) {
        unsafe {
            Self::do_init_io(
                this.as_mut_ptr().cast(),
                MaybeUninitField::parent_mut(this).cast(),
                &ops.0,
                name,
                size,
            );
        }
    }
}

unsafe impl ObjectType for MemoryRegion {
    type Class = bindings::MemoryRegionClass;
    const TYPE_NAME: &'static CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_MEMORY_REGION) };
}

qom_isa!(MemoryRegion: Object);

/// A special `MemTxAttrs` constant, used to indicate that no memory
/// attributes are specified.
///
/// Bus masters which don't specify any attributes will get this,
/// which has all attribute bits clear except the topmost one
/// (so that we can distinguish "all attributes deliberately clear"
/// from "didn't specify" if necessary).
pub const MEMTXATTRS_UNSPECIFIED: MemTxAttrs = MemTxAttrs {
    unspecified: true,
    ..Zeroable::ZERO
};
