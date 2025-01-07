// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Bindings to create devices and access device functionality from Rust.

use std::{
    ffi::{CStr, CString},
    os::raw::c_void,
    ptr::NonNull,
};

pub use bindings::{Clock, ClockEvent, DeviceClass, DeviceState, Property};

use crate::{
    bindings::{self, Error},
    callbacks::FnCall,
    cell::bql_locked,
    prelude::*,
    qom::{ClassInitImpl, ObjectClass, ObjectImpl, Owned},
    vmstate::VMStateDescription,
};

/// Trait providing the contents of [`DeviceClass`].
pub trait DeviceImpl: ObjectImpl {
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

/// Trait for methods exposed by the [`DeviceState`] class.  The methods can be
/// called on all objects that have the trait `IsA<DeviceState>`.
///
/// The trait should only be used through the blanket implementation,
/// which guarantees safety via `IsA`.
pub trait DeviceMethods: ObjectDeref
where
    Self::Target: IsA<DeviceState>,
{
    /// Add an input clock named `name`.  Invoke the callback with
    /// `self` as the first parameter for the events that are requested.
    ///
    /// The resulting clock is added as a child of `self`, but it also
    /// stays alive until after `Drop::drop` is called because C code
    /// keeps an extra reference to it until `device_finalize()` calls
    /// `qdev_finalize_clocklist()`.  Therefore (unlike most cases in
    /// which Rust code has a reference to a child object) it would be
    /// possible for this function to return a `&Clock` too.
    #[inline]
    fn init_clock_in<F: for<'a> FnCall<(&'a Self::Target, ClockEvent)>>(
        &self,
        name: &str,
        _cb: &F,
        events: ClockEvent,
    ) -> Owned<Clock> {
        fn do_init_clock_in(
            dev: *mut DeviceState,
            name: &str,
            cb: Option<unsafe extern "C" fn(*mut c_void, ClockEvent)>,
            events: ClockEvent,
        ) -> Owned<Clock> {
            assert!(bql_locked());

            // SAFETY: the clock is heap allocated, but qdev_init_clock_in()
            // does not gift the reference to its caller; so use Owned::from to
            // add one.  The callback is disabled automatically when the clock
            // is unparented, which happens before the device is finalized.
            unsafe {
                let cstr = CString::new(name).unwrap();
                let clk = bindings::qdev_init_clock_in(
                    dev,
                    cstr.as_ptr(),
                    cb,
                    dev.cast::<c_void>(),
                    events.0,
                );

                Owned::from(&*clk)
            }
        }

        let cb: Option<unsafe extern "C" fn(*mut c_void, ClockEvent)> = if F::is_some() {
            unsafe extern "C" fn rust_clock_cb<T, F: for<'a> FnCall<(&'a T, ClockEvent)>>(
                opaque: *mut c_void,
                event: ClockEvent,
            ) {
                // SAFETY: the opaque is "this", which is indeed a pointer to T
                F::call((unsafe { &*(opaque.cast::<T>()) }, event))
            }
            Some(rust_clock_cb::<Self::Target, F>)
        } else {
            None
        };

        do_init_clock_in(self.as_mut_ptr(), name, cb, events)
    }

    /// Add an output clock named `name`.
    ///
    /// The resulting clock is added as a child of `self`, but it also
    /// stays alive until after `Drop::drop` is called because C code
    /// keeps an extra reference to it until `device_finalize()` calls
    /// `qdev_finalize_clocklist()`.  Therefore (unlike most cases in
    /// which Rust code has a reference to a child object) it would be
    /// possible for this function to return a `&Clock` too.
    #[inline]
    fn init_clock_out(&self, name: &str) -> Owned<Clock> {
        unsafe {
            let cstr = CString::new(name).unwrap();
            let clk = bindings::qdev_init_clock_out(self.as_mut_ptr(), cstr.as_ptr());

            Owned::from(&*clk)
        }
    }
}

impl<R: ObjectDeref> DeviceMethods for R where R::Target: IsA<DeviceState> {}

unsafe impl ObjectType for Clock {
    type Class = ObjectClass;
    const TYPE_NAME: &'static CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_CLOCK) };
}
qom_isa!(Clock: Object);
