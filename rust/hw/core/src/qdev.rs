// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Bindings to create devices and access device functionality from Rust.

use std::{
    ffi::{c_int, c_void, CStr, CString},
    ptr::{addr_of, NonNull},
};

use chardev::Chardev;
use common::{callbacks::FnCall, Opaque};
use migration::{impl_vmstate_c_struct, VMStateDescription};
use qom::{prelude::*, ObjectClass, ObjectImpl, Owned, ParentInit};
use util::{Error, Result};

pub use crate::bindings::{ClockEvent, DeviceClass, Property, ResetType};
use crate::{
    bindings::{self, qdev_init_gpio_in, qdev_init_gpio_out, ResettableClass},
    irq::InterruptSource,
};

/// A safe wrapper around [`bindings::Clock`].
#[repr(transparent)]
#[derive(Debug, common::Wrapper)]
pub struct Clock(Opaque<bindings::Clock>);

unsafe impl Send for Clock {}
unsafe impl Sync for Clock {}

/// A safe wrapper around [`bindings::DeviceState`].
#[repr(transparent)]
#[derive(Debug, common::Wrapper)]
pub struct DeviceState(Opaque<bindings::DeviceState>);

unsafe impl Send for DeviceState {}
unsafe impl Sync for DeviceState {}

/// Trait providing the contents of the `ResettablePhases` struct,
/// which is part of the QOM `Resettable` interface.
pub trait ResettablePhasesImpl {
    /// If not None, this is called when the object enters reset. It
    /// can reset local state of the object, but it must not do anything that
    /// has a side-effect on other objects, such as raising or lowering an
    /// [`InterruptSource`], or reading or writing guest memory. It takes the
    /// reset's type as argument.
    const ENTER: Option<fn(&Self, ResetType)> = None;

    /// If not None, this is called when the object for entry into reset, once
    /// every object in the system which is being reset has had its
    /// `ResettablePhasesImpl::ENTER` method called. At this point devices
    /// can do actions that affect other objects.
    ///
    /// If in doubt, implement this method.
    const HOLD: Option<fn(&Self, ResetType)> = None;

    /// If not None, this phase is called when the object leaves the reset
    /// state. Actions affecting other objects are permitted.
    const EXIT: Option<fn(&Self, ResetType)> = None;
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer that
/// can be downcasted to type `T`. We also expect the device is
/// readable/writeable from one thread at any time.
unsafe extern "C" fn rust_resettable_enter_fn<T: ResettablePhasesImpl>(
    obj: *mut bindings::Object,
    typ: ResetType,
) {
    let state = NonNull::new(obj).unwrap().cast::<T>();
    T::ENTER.unwrap()(unsafe { state.as_ref() }, typ);
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer that
/// can be downcasted to type `T`. We also expect the device is
/// readable/writeable from one thread at any time.
unsafe extern "C" fn rust_resettable_hold_fn<T: ResettablePhasesImpl>(
    obj: *mut bindings::Object,
    typ: ResetType,
) {
    let state = NonNull::new(obj).unwrap().cast::<T>();
    T::HOLD.unwrap()(unsafe { state.as_ref() }, typ);
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer that
/// can be downcasted to type `T`. We also expect the device is
/// readable/writeable from one thread at any time.
unsafe extern "C" fn rust_resettable_exit_fn<T: ResettablePhasesImpl>(
    obj: *mut bindings::Object,
    typ: ResetType,
) {
    let state = NonNull::new(obj).unwrap().cast::<T>();
    T::EXIT.unwrap()(unsafe { state.as_ref() }, typ);
}

/// Helper trait to return pointer to a [`bindings::PropertyInfo`] for a type.
///
/// This trait is used by [`qemu_macros::Device`] derive macro.
///
/// Base types that already have `qdev_prop_*` globals in the QEMU API should
/// use those values as exported by the [`bindings`] module, instead of
/// redefining them.
///
/// # Safety
///
/// This trait is marked as `unsafe` because `BASE_INFO` and `BIT_INFO` must be
/// valid raw references to [`bindings::PropertyInfo`].
///
/// Note we could not use a regular reference:
///
/// ```text
/// const VALUE: &bindings::PropertyInfo = ...
/// ```
///
/// because this results in the following compiler error:
///
/// ```text
/// constructing invalid value: encountered reference to `extern` static in `const`
/// ```
///
/// This is because the compiler generally might dereference a normal reference
/// during const evaluation, but not in this case (if it did, it'd need to
/// dereference the raw pointer so using a `*const` would also fail to compile).
///
/// It is the implementer's responsibility to provide a valid
/// [`bindings::PropertyInfo`] pointer for the trait implementation to be safe.
pub unsafe trait QDevProp {
    const BASE_INFO: *const bindings::PropertyInfo;
    const BIT_INFO: *const bindings::PropertyInfo = {
        panic!("invalid type for bit property");
    };
}

macro_rules! impl_qdev_prop {
    ($type:ty,$info:ident$(, $bit_info:ident)?) => {
        unsafe impl $crate::qdev::QDevProp for $type {
            const BASE_INFO: *const $crate::bindings::PropertyInfo =
                addr_of!($crate::bindings::$info);
            $(const BIT_INFO: *const $crate::bindings::PropertyInfo =
                addr_of!($crate::bindings::$bit_info);)?
        }
    };
}

impl_qdev_prop!(bool, qdev_prop_bool);
impl_qdev_prop!(u8, qdev_prop_uint8);
impl_qdev_prop!(u16, qdev_prop_uint16);
impl_qdev_prop!(u32, qdev_prop_uint32, qdev_prop_bit);
impl_qdev_prop!(u64, qdev_prop_uint64, qdev_prop_bit64);
impl_qdev_prop!(usize, qdev_prop_usize);
impl_qdev_prop!(i32, qdev_prop_int32);
impl_qdev_prop!(i64, qdev_prop_int64);
impl_qdev_prop!(chardev::CharFrontend, qdev_prop_chr);

/// Trait to define device properties.
///
/// # Safety
///
/// Caller is responsible for the validity of properties array.
pub unsafe trait DevicePropertiesImpl {
    /// An array providing the properties that the user can set on the
    /// device.
    const PROPERTIES: &'static [Property] = &[];
}

/// Trait providing the contents of [`DeviceClass`].
pub trait DeviceImpl:
    ObjectImpl + ResettablePhasesImpl + DevicePropertiesImpl + IsA<DeviceState>
{
    /// _Realization_ is the second stage of device creation. It contains
    /// all operations that depend on device properties and can fail (note:
    /// this is not yet supported for Rust devices).
    ///
    /// If not `None`, the parent class's `realize` method is overridden
    /// with the function pointed to by `REALIZE`.
    const REALIZE: Option<fn(&Self) -> Result<()>> = None;

    /// A `VMStateDescription` providing the migration format for the device
    /// Not a `const` because referencing statics in constants is unstable
    /// until Rust 1.83.0.
    const VMSTATE: Option<VMStateDescription<Self>> = None;
}

/// # Safety
///
/// This function is only called through the QOM machinery and
/// used by `DeviceClass::class_init`.
/// We expect the FFI user of this function to pass a valid pointer that
/// can be downcasted to type `T`. We also expect the device is
/// readable/writeable from one thread at any time.
unsafe extern "C" fn rust_realize_fn<T: DeviceImpl>(
    dev: *mut bindings::DeviceState,
    errp: *mut *mut util::bindings::Error,
) {
    let state = NonNull::new(dev).unwrap().cast::<T>();
    let result = T::REALIZE.unwrap()(unsafe { state.as_ref() });
    unsafe {
        Error::ok_or_propagate(result, errp);
    }
}

unsafe impl InterfaceType for ResettableClass {
    const TYPE_NAME: &'static CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_RESETTABLE_INTERFACE) };
}

impl ResettableClass {
    /// Fill in the virtual methods of `ResettableClass` based on the
    /// definitions in the `ResettablePhasesImpl` trait.
    pub fn class_init<T: ResettablePhasesImpl>(&mut self) {
        if <T as ResettablePhasesImpl>::ENTER.is_some() {
            self.phases.enter = Some(rust_resettable_enter_fn::<T>);
        }
        if <T as ResettablePhasesImpl>::HOLD.is_some() {
            self.phases.hold = Some(rust_resettable_hold_fn::<T>);
        }
        if <T as ResettablePhasesImpl>::EXIT.is_some() {
            self.phases.exit = Some(rust_resettable_exit_fn::<T>);
        }
    }
}

impl DeviceClass {
    /// Fill in the virtual methods of `DeviceClass` based on the definitions in
    /// the `DeviceImpl` trait.
    pub fn class_init<T: DeviceImpl>(&mut self) {
        if <T as DeviceImpl>::REALIZE.is_some() {
            self.realize = Some(rust_realize_fn::<T>);
        }
        if let Some(ref vmsd) = <T as DeviceImpl>::VMSTATE {
            self.vmsd = vmsd.as_ref();
        }
        let prop = <T as DevicePropertiesImpl>::PROPERTIES;
        if !prop.is_empty() {
            unsafe {
                bindings::device_class_set_props_n(self, prop.as_ptr(), prop.len());
            }
        }

        ResettableClass::cast::<DeviceState>(self).class_init::<T>();
        self.parent_class.class_init::<T>();
    }
}

unsafe impl ObjectType for DeviceState {
    type Class = DeviceClass;
    const TYPE_NAME: &'static CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_DEVICE) };
}

qom_isa!(DeviceState: Object);

/// Initialization methods take a [`ParentInit`] and can be called as
/// associated functions.
impl DeviceState {
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
    pub fn init_clock_in<T: DeviceImpl, F: for<'a> FnCall<(&'a T, ClockEvent)>>(
        this: &mut ParentInit<T>,
        name: &str,
        _cb: &F,
        events: ClockEvent,
    ) -> Owned<Clock>
    where
        T::ParentType: IsA<DeviceState>,
    {
        fn do_init_clock_in(
            dev: &DeviceState,
            name: &str,
            cb: Option<unsafe extern "C" fn(*mut c_void, ClockEvent)>,
            events: ClockEvent,
        ) -> Owned<Clock> {
            assert!(bql::is_locked());

            // SAFETY: the clock is heap allocated, but qdev_init_clock_in()
            // does not gift the reference to its caller; so use Owned::from to
            // add one.  The callback is disabled automatically when the clock
            // is unparented, which happens before the device is finalized.
            unsafe {
                let cstr = CString::new(name).unwrap();
                let clk = bindings::qdev_init_clock_in(
                    dev.0.as_mut_ptr(),
                    cstr.as_ptr(),
                    cb,
                    dev.0.as_void_ptr(),
                    events.0,
                );

                let clk: &Clock = Clock::from_raw(clk);
                Owned::from(clk)
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
            Some(rust_clock_cb::<T, F>)
        } else {
            None
        };

        do_init_clock_in(unsafe { this.upcast_mut() }, name, cb, events)
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
    pub fn init_clock_out<T: DeviceImpl>(this: &mut ParentInit<T>, name: &str) -> Owned<Clock>
    where
        T::ParentType: IsA<DeviceState>,
    {
        unsafe {
            let cstr = CString::new(name).unwrap();
            let dev: &mut DeviceState = this.upcast_mut();
            let clk = bindings::qdev_init_clock_out(dev.0.as_mut_ptr(), cstr.as_ptr());

            let clk: &Clock = Clock::from_raw(clk);
            Owned::from(clk)
        }
    }
}

/// Trait for methods exposed by the [`DeviceState`] class.  The methods can be
/// called on all objects that have the trait `IsA<DeviceState>`.
///
/// The trait should only be used through the blanket implementation,
/// which guarantees safety via `IsA`.
pub trait DeviceMethods: ObjectDeref
where
    Self::Target: IsA<DeviceState>,
{
    fn prop_set_chr(&self, propname: &str, chr: &Owned<Chardev>) {
        assert!(bql::is_locked());
        let c_propname = CString::new(propname).unwrap();
        let chr: &Chardev = chr;
        unsafe {
            bindings::qdev_prop_set_chr(
                self.upcast().as_mut_ptr(),
                c_propname.as_ptr(),
                chr.as_mut_ptr(),
            );
        }
    }

    fn init_gpio_in<F: for<'a> FnCall<(&'a Self::Target, u32, u32)>>(
        &self,
        num_lines: u32,
        _cb: F,
    ) {
        fn do_init_gpio_in(
            dev: &DeviceState,
            num_lines: u32,
            gpio_in_cb: unsafe extern "C" fn(*mut c_void, c_int, c_int),
        ) {
            unsafe {
                qdev_init_gpio_in(dev.as_mut_ptr(), Some(gpio_in_cb), num_lines as c_int);
            }
        }

        const { assert!(F::IS_SOME) };
        unsafe extern "C" fn rust_irq_handler<T, F: for<'a> FnCall<(&'a T, u32, u32)>>(
            opaque: *mut c_void,
            line: c_int,
            level: c_int,
        ) {
            // SAFETY: the opaque was passed as a reference to `T`
            F::call((unsafe { &*(opaque.cast::<T>()) }, line as u32, level as u32))
        }

        let gpio_in_cb: unsafe extern "C" fn(*mut c_void, c_int, c_int) =
            rust_irq_handler::<Self::Target, F>;

        do_init_gpio_in(self.upcast(), num_lines, gpio_in_cb);
    }

    fn init_gpio_out(&self, pins: &[InterruptSource]) {
        unsafe {
            qdev_init_gpio_out(
                self.upcast().as_mut_ptr(),
                InterruptSource::slice_as_ptr(pins),
                pins.len() as c_int,
            );
        }
    }
}

impl<R: ObjectDeref> DeviceMethods for R where R::Target: IsA<DeviceState> {}

impl Clock {
    pub const PERIOD_1SEC: u64 = bindings::CLOCK_PERIOD_1SEC;

    pub const fn period_from_ns(ns: u64) -> u64 {
        ns * Self::PERIOD_1SEC / 1_000_000_000
    }

    pub const fn period_from_hz(hz: u64) -> u64 {
        if hz == 0 {
            0
        } else {
            Self::PERIOD_1SEC / hz
        }
    }

    pub const fn period_to_hz(period: u64) -> u64 {
        if period == 0 {
            0
        } else {
            Self::PERIOD_1SEC / period
        }
    }

    pub const fn period(&self) -> u64 {
        // SAFETY: Clock is returned by init_clock_in with zero value for period
        unsafe { &*self.0.as_ptr() }.period
    }

    pub const fn hz(&self) -> u64 {
        Self::period_to_hz(self.period())
    }
}

unsafe impl ObjectType for Clock {
    type Class = ObjectClass;
    const TYPE_NAME: &'static CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_CLOCK) };
}

qom_isa!(Clock: Object);

impl_vmstate_c_struct!(Clock, bindings::vmstate_clock);
