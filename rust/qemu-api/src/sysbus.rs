// Copyright 2024 Red Hat, Inc.
// Author(s): Paolo Bonzini <pbonzini@redhat.com>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::{ffi::CStr, ptr::addr_of};

pub use bindings::{SysBusDevice, SysBusDeviceClass};

use crate::{
    bindings,
    cell::bql_locked,
    irq::InterruptSource,
    prelude::*,
    qdev::{DeviceClass, DeviceState},
    qom::ClassInitImpl,
};

unsafe impl ObjectType for SysBusDevice {
    type Class = SysBusDeviceClass;
    const TYPE_NAME: &'static CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_SYS_BUS_DEVICE) };
}
qom_isa!(SysBusDevice: DeviceState, Object);

// TODO: add SysBusDeviceImpl
impl<T> ClassInitImpl<SysBusDeviceClass> for T
where
    T: ClassInitImpl<DeviceClass>,
{
    fn class_init(sdc: &mut SysBusDeviceClass) {
        <T as ClassInitImpl<DeviceClass>>::class_init(&mut sdc.parent_class);
    }
}

impl SysBusDevice {
    /// Return `self` cast to a mutable pointer, for use in calls to C code.
    const fn as_mut_ptr(&self) -> *mut SysBusDevice {
        addr_of!(*self) as *mut _
    }

    /// Expose an interrupt source outside the device as a qdev GPIO output.
    /// Note that the ordering of calls to `init_irq` is important, since
    /// whoever creates the sysbus device will refer to the interrupts with
    /// a number that corresponds to the order of calls to `init_irq`.
    pub fn init_irq(&self, irq: &InterruptSource) {
        assert!(bql_locked());
        unsafe {
            bindings::sysbus_init_irq(self.as_mut_ptr(), irq.as_ptr());
        }
    }
}
