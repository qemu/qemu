// Copyright 2024 Red Hat, Inc.
// Author(s): Paolo Bonzini <pbonzini@redhat.com>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Bindings to access `sysbus` functionality from Rust.

use std::ffi::CStr;

pub use bindings::SysBusDeviceClass;
use common::Opaque;
use qom::{prelude::*, Owned};
use system::MemoryRegion;
use util::{Error, Result};

use crate::{
    bindings,
    irq::{IRQState, InterruptSource},
    qdev::{DeviceImpl, DeviceState},
};

/// A safe wrapper around [`bindings::SysBusDevice`].
#[repr(transparent)]
#[derive(Debug, common::Wrapper)]
pub struct SysBusDevice(Opaque<bindings::SysBusDevice>);

unsafe impl Send for SysBusDevice {}
unsafe impl Sync for SysBusDevice {}

unsafe impl ObjectType for SysBusDevice {
    type Class = SysBusDeviceClass;
    const TYPE_NAME: &'static CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_SYS_BUS_DEVICE) };
}

qom_isa!(SysBusDevice: DeviceState, Object);

// TODO: add virtual methods
pub trait SysBusDeviceImpl: DeviceImpl + IsA<SysBusDevice> {}

impl SysBusDeviceClass {
    /// Fill in the virtual methods of `SysBusDeviceClass` based on the
    /// definitions in the `SysBusDeviceImpl` trait.
    pub fn class_init<T: SysBusDeviceImpl>(self: &mut SysBusDeviceClass) {
        self.parent_class.class_init::<T>();
    }
}

/// Trait for methods of [`SysBusDevice`] and its subclasses.
pub trait SysBusDeviceMethods: ObjectDeref
where
    Self::Target: IsA<SysBusDevice>,
{
    /// Expose a memory region to the board so that it can give it an address
    /// in guest memory.  Note that the ordering of calls to `init_mmio` is
    /// important, since whoever creates the sysbus device will refer to the
    /// region with a number that corresponds to the order of calls to
    /// `init_mmio`.
    fn init_mmio(&self, iomem: &MemoryRegion) {
        assert!(bql::is_locked());
        unsafe {
            bindings::sysbus_init_mmio(self.upcast().as_mut_ptr(), iomem.as_mut_ptr());
        }
    }

    /// Expose an interrupt source outside the device as a qdev GPIO output.
    /// Note that the ordering of calls to `init_irq` is important, since
    /// whoever creates the sysbus device will refer to the interrupts with
    /// a number that corresponds to the order of calls to `init_irq`.
    fn init_irq(&self, irq: &InterruptSource) {
        assert!(bql::is_locked());
        unsafe {
            bindings::sysbus_init_irq(self.upcast().as_mut_ptr(), irq.as_ptr());
        }
    }

    // TODO: do we want a type like GuestAddress here?
    fn mmio_addr(&self, id: u32) -> Option<u64> {
        assert!(bql::is_locked());
        // SAFETY: the BQL ensures that no one else writes to sbd.mmio[], and
        // the SysBusDevice must be initialized to get an IsA<SysBusDevice>.
        let sbd = unsafe { *self.upcast().as_ptr() };
        let id: usize = id.try_into().unwrap();
        if sbd.mmio[id].memory.is_null() {
            None
        } else {
            Some(sbd.mmio[id].addr)
        }
    }

    // TODO: do we want a type like GuestAddress here?
    fn mmio_map(&self, id: u32, addr: u64) {
        assert!(bql::is_locked());
        let id: i32 = id.try_into().unwrap();
        unsafe {
            bindings::sysbus_mmio_map(self.upcast().as_mut_ptr(), id, addr);
        }
    }

    // Owned<> is used here because sysbus_connect_irq (via
    // object_property_set_link) adds a reference to the IRQState,
    // which can prolong its life
    fn connect_irq(&self, id: u32, irq: &Owned<IRQState>) {
        assert!(bql::is_locked());
        let id: i32 = id.try_into().unwrap();
        let irq: &IRQState = irq;
        unsafe {
            bindings::sysbus_connect_irq(self.upcast().as_mut_ptr(), id, irq.as_mut_ptr());
        }
    }

    fn sysbus_realize(&self) -> Result<()> {
        assert!(bql::is_locked());
        unsafe {
            Error::with_errp(|errp| {
                bindings::sysbus_realize(self.upcast().as_mut_ptr(), errp);
            })
        }
    }
}

impl<R: ObjectDeref> SysBusDeviceMethods for R where R::Target: IsA<SysBusDevice> {}
