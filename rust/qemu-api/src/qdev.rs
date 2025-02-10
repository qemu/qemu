// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Bindings to create devices and access device functionality from Rust.

use std::{ffi::CStr, ptr::NonNull};

pub use bindings::{DeviceClass, DeviceState, Property};

use crate::{
    bindings::{self, Error},
    prelude::*,
    qom::{ClassInitImpl, ObjectClass},
    vmstate::VMStateDescription,
};

/// Trait providing the contents of [`DeviceClass`].
pub trait DeviceImpl {
    /// _Realization_ is the second stage of device creation. It contains
    /// all operations that depend on device properties and can fail (note:
    /// this is not yet supported for Rust devices).
    ///
    /// If not `None`, the parent class's `realize` method is overridden
    /// with the function pointed to by `REALIZE`.
    const REALIZE: Option<fn(&Self)> = None;

    /// If not `None`, the parent class's `reset` method is overridden
    /// with the function pointed to by `RESET`.
    ///
    /// Rust does not yet support the three-phase reset protocol; this is
    /// usually okay for leaf classes.
    const RESET: Option<fn(&Self)> = None;

    /// An array providing the properties that the user can set on the
    /// device.  Not a `const` because referencing statics in constants
    /// is unstable until Rust 1.83.0.
    fn properties() -> &'static [Property] {
        &[]
    }

    /// A `VMStateDescription` providing the migration format for the device
    /// Not a `const` because referencing statics in constants is unstable
    /// until Rust 1.83.0.
    fn vmsd() -> Option<&'static VMStateDescription> {
        None
    }
}

/// # Safety
///
/// This function is only called through the QOM machinery and
/// used by the `ClassInitImpl<DeviceClass>` trait.
/// We expect the FFI user of this function to pass a valid pointer that
/// can be downcasted to type `T`. We also expect the device is
/// readable/writeable from one thread at any time.
unsafe extern "C" fn rust_realize_fn<T: DeviceImpl>(dev: *mut DeviceState, _errp: *mut *mut Error) {
    let state = NonNull::new(dev).unwrap().cast::<T>();
    T::REALIZE.unwrap()(unsafe { state.as_ref() });
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer that
/// can be downcasted to type `T`. We also expect the device is
/// readable/writeable from one thread at any time.
unsafe extern "C" fn rust_reset_fn<T: DeviceImpl>(dev: *mut DeviceState) {
    let mut state = NonNull::new(dev).unwrap().cast::<T>();
    T::RESET.unwrap()(unsafe { state.as_mut() });
}

impl<T> ClassInitImpl<DeviceClass> for T
where
    T: ClassInitImpl<ObjectClass> + DeviceImpl,
{
    fn class_init(dc: &mut DeviceClass) {
        if <T as DeviceImpl>::REALIZE.is_some() {
            dc.realize = Some(rust_realize_fn::<T>);
        }
        if <T as DeviceImpl>::RESET.is_some() {
            unsafe {
                bindings::device_class_set_legacy_reset(dc, Some(rust_reset_fn::<T>));
            }
        }
        if let Some(vmsd) = <T as DeviceImpl>::vmsd() {
            dc.vmsd = vmsd;
        }
        let prop = <T as DeviceImpl>::properties();
        if !prop.is_empty() {
            unsafe {
                bindings::device_class_set_props_n(dc, prop.as_ptr(), prop.len());
            }
        }

        <T as ClassInitImpl<ObjectClass>>::class_init(&mut dc.parent_class);
    }
}

#[macro_export]
macro_rules! define_property {
    ($name:expr, $state:ty, $field:ident, $prop:expr, $type:ty, default = $defval:expr$(,)*) => {
        $crate::bindings::Property {
            // use associated function syntax for type checking
            name: ::std::ffi::CStr::as_ptr($name),
            info: $prop,
            offset: $crate::offset_of!($state, $field) as isize,
            set_default: true,
            defval: $crate::bindings::Property__bindgen_ty_1 { u: $defval as u64 },
            ..$crate::zeroable::Zeroable::ZERO
        }
    };
    ($name:expr, $state:ty, $field:ident, $prop:expr, $type:ty$(,)*) => {
        $crate::bindings::Property {
            // use associated function syntax for type checking
            name: ::std::ffi::CStr::as_ptr($name),
            info: $prop,
            offset: $crate::offset_of!($state, $field) as isize,
            set_default: false,
            ..$crate::zeroable::Zeroable::ZERO
        }
    };
}

#[macro_export]
macro_rules! declare_properties {
    ($ident:ident, $($prop:expr),*$(,)*) => {
        pub static $ident: [$crate::bindings::Property; {
            let mut len = 0;
            $({
                _ = stringify!($prop);
                len += 1;
            })*
            len
        }] = [
            $($prop),*,
        ];
    };
}

unsafe impl ObjectType for DeviceState {
    type Class = DeviceClass;
    const TYPE_NAME: &'static CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_DEVICE) };
}
qom_isa!(DeviceState: Object);
