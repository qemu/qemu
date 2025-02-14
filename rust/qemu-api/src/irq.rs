// Copyright 2024 Red Hat, Inc.
// Author(s): Paolo Bonzini <pbonzini@redhat.com>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Bindings for interrupt sources

use std::{ffi::CStr, marker::PhantomData, os::raw::c_int, ptr};

use crate::{
    bindings::{self, qemu_set_irq},
    cell::Opaque,
    prelude::*,
    qom::ObjectClass,
};

/// An opaque wrapper around [`bindings::IRQState`].
#[repr(transparent)]
#[derive(Debug, qemu_api_macros::Wrapper)]
pub struct IRQState(Opaque<bindings::IRQState>);

/// Interrupt sources are used by devices to pass changes to a value (typically
/// a boolean).  The interrupt sink is usually an interrupt controller or
/// GPIO controller.
///
/// As far as devices are concerned, interrupt sources are always active-high:
/// for example, `InterruptSource<bool>`'s [`raise`](InterruptSource::raise)
/// method sends a `true` value to the sink.  If the guest has to see a
/// different polarity, that change is performed by the board between the
/// device and the interrupt controller.
///
/// Interrupts are implemented as a pointer to the interrupt "sink", which has
/// type [`IRQState`].  A device exposes its source as a QOM link property using
/// a function such as [`SysBusDeviceMethods::init_irq`], and
/// initially leaves the pointer to a NULL value, representing an unconnected
/// interrupt. To connect it, whoever creates the device fills the pointer with
/// the sink's `IRQState *`, for example using `sysbus_connect_irq`.  Because
/// devices are generally shared objects, interrupt sources are an example of
/// the interior mutability pattern.
///
/// Interrupt sources can only be triggered under the Big QEMU Lock; `BqlCell`
/// allows access from whatever thread has it.
#[derive(Debug)]
#[repr(transparent)]
pub struct InterruptSource<T = bool>
where
    c_int: From<T>,
{
    cell: BqlCell<*mut bindings::IRQState>,
    _marker: PhantomData<T>,
}

// SAFETY: the implementation asserts via `BqlCell` that the BQL is taken
unsafe impl<T> Sync for InterruptSource<T> where c_int: From<T> {}

impl InterruptSource<bool> {
    /// Send a low (`false`) value to the interrupt sink.
    pub fn lower(&self) {
        self.set(false);
    }

    /// Send a high-low pulse to the interrupt sink.
    pub fn pulse(&self) {
        self.set(true);
        self.set(false);
    }

    /// Send a high (`true`) value to the interrupt sink.
    pub fn raise(&self) {
        self.set(true);
    }
}

impl<T> InterruptSource<T>
where
    c_int: From<T>,
{
    /// Send `level` to the interrupt sink.
    pub fn set(&self, level: T) {
        let ptr = self.cell.get();
        // SAFETY: the pointer is retrieved under the BQL and remains valid
        // until the BQL is released, which is after qemu_set_irq() is entered.
        unsafe {
            qemu_set_irq(ptr, level.into());
        }
    }

    pub(crate) const fn as_ptr(&self) -> *mut *mut bindings::IRQState {
        self.cell.as_ptr()
    }

    pub(crate) const fn slice_as_ptr(slice: &[Self]) -> *mut *mut bindings::IRQState {
        assert!(!slice.is_empty());
        slice[0].as_ptr()
    }
}

impl Default for InterruptSource {
    fn default() -> Self {
        InterruptSource {
            cell: BqlCell::new(ptr::null_mut()),
            _marker: PhantomData,
        }
    }
}

unsafe impl ObjectType for IRQState {
    type Class = ObjectClass;
    const TYPE_NAME: &'static CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_IRQ) };
}
qom_isa!(IRQState: Object);
