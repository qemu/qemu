// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Helper macros to declare migration state for device models.
//!
//! This module includes four families of macros:
//!
//! * [`vmstate_unused!`](crate::vmstate_unused) and
//!   [`vmstate_of!`](crate::vmstate_of), which are used to express the
//!   migration format for a struct.  This is based on the [`VMState`] trait,
//!   which is defined by all migratable types.
//!
//! * [`impl_vmstate_forward`](crate::impl_vmstate_forward),
//!   [`impl_vmstate_bitsized`](crate::impl_vmstate_bitsized), and
//!   [`impl_vmstate_struct`](crate::impl_vmstate_struct), which help with the
//!   definition of the [`VMState`] trait (respectively for transparent structs,
//!   nested structs and `bilge`-defined types)
//!
//! * helper macros to declare a device model state struct, in particular
//!   [`vmstate_subsections`](crate::vmstate_subsections) and
//!   [`vmstate_fields`](crate::vmstate_fields).
//!
//! * direct equivalents to the C macros declared in
//!   `include/migration/vmstate.h`. These are not type-safe and only provide
//!   functionality that is missing from `vmstate_of!`.

pub use std::convert::Infallible;
use std::{
    error::Error,
    ffi::{c_int, c_void, CStr},
    fmt, io,
    marker::PhantomData,
    mem,
    ptr::{addr_of, NonNull},
};

use common::{
    callbacks::FnCall,
    errno::{into_neg_errno, Errno},
    Zeroable,
};

use crate::bindings::{self, VMStateFlags};
pub use crate::bindings::{MigrationPriority, VMStateField};

/// This macro is used to call a function with a generic argument bound
/// to the type of a field.  The function must take a
/// [`PhantomData`]`<T>` argument; `T` is the type of
/// field `$field` in the `$typ` type.
///
/// # Examples
///
/// ```
/// # use migration::call_func_with_field;
/// # use core::marker::PhantomData;
/// const fn size_of_field<T>(_: PhantomData<T>) -> usize {
///     std::mem::size_of::<T>()
/// }
///
/// struct Foo {
///     x: u16,
/// };
/// // calls size_of_field::<u16>()
/// assert_eq!(call_func_with_field!(size_of_field, Foo, x), 2);
/// ```
#[macro_export]
macro_rules! call_func_with_field {
    // Based on the answer by user steffahn (Frank Steffahn) at
    // https://users.rust-lang.org/t/inferring-type-of-field/122857
    // and used under MIT license
    ($func:expr, $typ:ty, $($field:tt).+) => {
        $func(loop {
            #![allow(unreachable_code)]
            #![allow(unused_variables)]
            const fn phantom__<T>(_: &T) -> ::core::marker::PhantomData<T> { ::core::marker::PhantomData }
            // Unreachable code is exempt from checks on uninitialized values.
            // Use that trick to infer the type of this PhantomData.
            break ::core::marker::PhantomData;
            break phantom__(&{ let value__: $typ; value__.$($field).+ });
        })
    };
}

/// A trait for types that can be included in a device's migration stream.  It
/// provides the base contents of a `VMStateField` (minus the name and offset).
///
/// # Safety
///
/// The contents of this trait go straight into structs that are parsed by C
/// code and used to introspect into other structs.  Generally, you don't need
/// to implement it except via macros that do it for you, such as
/// `impl_vmstate_bitsized!`.
pub unsafe trait VMState {
    /// The base contents of a `VMStateField` (minus the name and offset) for
    /// the type that is implementing the trait.
    const BASE: VMStateField;

    /// A flag that is added to another field's `VMStateField` to specify the
    /// length's type in a variable-sized array.  If this is not a supported
    /// type for the length (i.e. if it is not `u8`, `u16`, `u32`), using it
    /// in a call to [`vmstate_of!`](crate::vmstate_of) will cause a
    /// compile-time error.
    const VARRAY_FLAG: VMStateFlags = {
        panic!("invalid type for variable-sized array");
    };
}

/// Internal utility function to retrieve a type's `VMStateField`;
/// used by [`vmstate_of!`](crate::vmstate_of).
pub const fn vmstate_base<T: VMState>(_: PhantomData<T>) -> VMStateField {
    T::BASE
}

/// Internal utility function to retrieve a type's `VMStateFlags` when it
/// is used as the element count of a `VMSTATE_VARRAY`; used by
/// [`vmstate_of!`](crate::vmstate_of).
pub const fn vmstate_varray_flag<T: VMState>(_: PhantomData<T>) -> VMStateFlags {
    T::VARRAY_FLAG
}

/// Return the `VMStateField` for a field of a struct.  The field must be
/// visible in the current scope.
///
/// Only a limited set of types is supported out of the box:
/// * scalar types (integer and `bool`)
/// * the C struct `QEMUTimer`
/// * a transparent wrapper for any of the above (`Cell`, `UnsafeCell`,
///   [`BqlCell`], [`BqlRefCell`])
/// * a raw pointer to any of the above
/// * a `NonNull` pointer, a `Box` or an [`Owned`] for any of the above
/// * an array of any of the above
///
/// In order to support other types, the trait `VMState` must be implemented
/// for them.  The macros [`impl_vmstate_forward`](crate::impl_vmstate_forward),
/// [`impl_vmstate_bitsized`](crate::impl_vmstate_bitsized), and
/// [`impl_vmstate_struct`](crate::impl_vmstate_struct) help with this.
///
/// [`BqlCell`]: ../../bql/cell/struct.BqlCell.html
/// [`BqlRefCell`]: ../../bql/cell/struct.BqlRefCell.html
/// [`Owned`]: ../../qom/qom/struct.Owned.html
#[macro_export]
macro_rules! vmstate_of {
    ($struct_name:ty, $($field_name:ident).+ $([0 .. $($num:ident).+ $(* $factor:expr)?])? $(, $test_fn:expr)? $(,)?) => {
        $crate::bindings::VMStateField {
            name: ::core::concat!(::core::stringify!($($field_name).+), "\0")
                .as_bytes()
                .as_ptr().cast::<::std::os::raw::c_char>(),
            offset: ::std::mem::offset_of!($struct_name, $($field_name).+),
            $(num_offset: ::std::mem::offset_of!($struct_name, $($num).+),)?
            $(field_exists: $crate::vmstate_exist_fn!($struct_name, $test_fn),)?
            // The calls to `call_func_with_field!` are the magic that
            // computes most of the VMStateField from the type of the field.
            ..$crate::call_func_with_field!(
                $crate::vmstate::vmstate_base,
                $struct_name,
                $($field_name).+
            )$(.with_varray_flag($crate::call_func_with_field!(
                    $crate::vmstate::vmstate_varray_flag,
                    $struct_name,
                    $($num).+))
               $(.with_varray_multiply($factor))?)?
        }
    };
}

pub trait VMStateFlagsExt {
    const VMS_VARRAY_FLAGS: VMStateFlags;
}

impl VMStateFlagsExt for VMStateFlags {
    const VMS_VARRAY_FLAGS: VMStateFlags = VMStateFlags(
        VMStateFlags::VMS_VARRAY_INT32.0
            | VMStateFlags::VMS_VARRAY_UINT8.0
            | VMStateFlags::VMS_VARRAY_UINT16.0
            | VMStateFlags::VMS_VARRAY_UINT32.0,
    );
}

// Add a couple builder-style methods to VMStateField, allowing
// easy derivation of VMStateField constants from other types.
impl VMStateField {
    #[must_use]
    pub const fn with_version_id(mut self, version_id: i32) -> Self {
        assert!(version_id >= 0);
        self.version_id = version_id;
        self
    }

    #[must_use]
    pub const fn with_array_flag(mut self, num: usize) -> Self {
        assert!(num <= 0x7FFF_FFFFusize);
        assert!((self.flags.0 & VMStateFlags::VMS_ARRAY.0) == 0);
        assert!((self.flags.0 & VMStateFlags::VMS_VARRAY_FLAGS.0) == 0);
        if (self.flags.0 & VMStateFlags::VMS_POINTER.0) != 0 {
            self.flags = VMStateFlags(self.flags.0 & !VMStateFlags::VMS_POINTER.0);
            self.flags = VMStateFlags(self.flags.0 | VMStateFlags::VMS_ARRAY_OF_POINTER.0);
            // VMS_ARRAY_OF_POINTER flag stores the size of pointer.
            // FIXME: *const, *mut, NonNull and Box<> have the same size as usize.
            //        Resize if more smart pointers are supported.
            self.size = std::mem::size_of::<usize>();
        }
        self.flags = VMStateFlags(self.flags.0 & !VMStateFlags::VMS_SINGLE.0);
        self.flags = VMStateFlags(self.flags.0 | VMStateFlags::VMS_ARRAY.0);
        self.num = num as i32;
        self
    }

    #[must_use]
    pub const fn with_pointer_flag(mut self) -> Self {
        assert!((self.flags.0 & VMStateFlags::VMS_POINTER.0) == 0);
        self.flags = VMStateFlags(self.flags.0 | VMStateFlags::VMS_POINTER.0);
        self
    }

    #[must_use]
    pub const fn with_varray_flag_unchecked(mut self, flag: VMStateFlags) -> Self {
        self.flags = VMStateFlags(self.flags.0 & !VMStateFlags::VMS_ARRAY.0);
        self.flags = VMStateFlags(self.flags.0 | flag.0);
        self.num = 0; // varray uses num_offset instead of num.
        self
    }

    #[must_use]
    #[allow(unused_mut)]
    pub const fn with_varray_flag(mut self, flag: VMStateFlags) -> Self {
        assert!((self.flags.0 & VMStateFlags::VMS_ARRAY.0) != 0);
        self.with_varray_flag_unchecked(flag)
    }

    #[must_use]
    pub const fn with_varray_multiply(mut self, num: u32) -> Self {
        assert!(num <= 0x7FFF_FFFFu32);
        self.flags = VMStateFlags(self.flags.0 | VMStateFlags::VMS_MULTIPLY_ELEMENTS.0);
        self.num = num as i32;
        self
    }
}

/// This macro can be used (by just passing it a type) to forward the `VMState`
/// trait to the first field of a tuple.  This is a workaround for lack of
/// support of nested [`offset_of`](core::mem::offset_of) until Rust 1.82.0.
///
/// # Examples
///
/// ```
/// # use migration::impl_vmstate_forward;
/// pub struct Fifo([u8; 16]);
/// impl_vmstate_forward!(Fifo);
/// ```
#[macro_export]
macro_rules! impl_vmstate_forward {
    // This is similar to impl_vmstate_transparent below, but it
    // uses the same trick as vmstate_of! to obtain the type of
    // the first field of the tuple
    ($tuple:ty) => {
        unsafe impl $crate::vmstate::VMState for $tuple {
            const BASE: $crate::bindings::VMStateField =
                $crate::call_func_with_field!($crate::vmstate::vmstate_base, $tuple, 0);
        }
    };
}

// Transparent wrappers: just use the internal type

#[macro_export]
macro_rules! impl_vmstate_transparent {
    ($type:ty where $base:tt: VMState $($where:tt)*) => {
        unsafe impl<$base> $crate::vmstate::VMState for $type where $base: $crate::vmstate::VMState $($where)* {
            const BASE: $crate::vmstate::VMStateField = $crate::vmstate::VMStateField {
                size: ::core::mem::size_of::<$type>(),
                ..<$base as $crate::vmstate::VMState>::BASE
            };
            const VARRAY_FLAG: $crate::bindings::VMStateFlags = <$base as $crate::vmstate::VMState>::VARRAY_FLAG;
        }
    };
}

impl_vmstate_transparent!(bql::BqlCell<T> where T: VMState);
impl_vmstate_transparent!(bql::BqlRefCell<T> where T: VMState);
impl_vmstate_transparent!(std::cell::Cell<T> where T: VMState);
impl_vmstate_transparent!(std::cell::UnsafeCell<T> where T: VMState);
impl_vmstate_transparent!(std::pin::Pin<T> where T: VMState);
impl_vmstate_transparent!(common::Opaque<T> where T: VMState);
impl_vmstate_transparent!(std::mem::ManuallyDrop<T> where T: VMState);

#[macro_export]
macro_rules! impl_vmstate_bitsized {
    ($type:ty) => {
        unsafe impl $crate::vmstate::VMState for $type {
            const BASE: $crate::bindings::VMStateField =
                                        <<<$type as ::bilge::prelude::Bitsized>::ArbitraryInt
                                          as ::bilge::prelude::Number>::UnderlyingType
                                         as $crate::vmstate::VMState>::BASE;
            const VARRAY_FLAG: $crate::bindings::VMStateFlags =
                                        <<<$type as ::bilge::prelude::Bitsized>::ArbitraryInt
                                          as ::bilge::prelude::Number>::UnderlyingType
                                         as $crate::vmstate::VMState>::VARRAY_FLAG;
        }

        impl $crate::migratable::ToMigrationState for $type {
            type Migrated = <<$type as ::bilge::prelude::Bitsized>::ArbitraryInt
                                          as ::bilge::prelude::Number>::UnderlyingType;

            fn snapshot_migration_state(&self, target: &mut Self::Migrated) -> Result<(), $crate::InvalidError> {
                *target = Self::Migrated::from(*self);
                Ok(())
            }

            fn restore_migrated_state_mut(
                &mut self,
                source: Self::Migrated,
                version_id: u8,
            ) -> Result<(), $crate::InvalidError> {
                *self = Self::from(source);
                Ok(())
            }
        }
    };
}

// Scalar types using predefined VMStateInfos

macro_rules! impl_vmstate_scalar {
    ($info:ident, $type:ty$(, $varray_flag:ident)?) => {
        unsafe impl $crate::vmstate::VMState for $type {
            const BASE: $crate::vmstate::VMStateField = $crate::vmstate::VMStateField {
                info: addr_of!(bindings::$info),
                size: mem::size_of::<$type>(),
                flags: $crate::vmstate::VMStateFlags::VMS_SINGLE,
                ..::common::zeroable::Zeroable::ZERO
            };
            $(const VARRAY_FLAG: VMStateFlags = VMStateFlags::$varray_flag;)?
        }
    };
}

impl_vmstate_scalar!(vmstate_info_bool, bool);
impl_vmstate_scalar!(vmstate_info_int8, i8);
impl_vmstate_scalar!(vmstate_info_int16, i16);
impl_vmstate_scalar!(vmstate_info_int32, i32);
impl_vmstate_scalar!(vmstate_info_int64, i64);
impl_vmstate_scalar!(vmstate_info_uint8, u8, VMS_VARRAY_UINT8);
impl_vmstate_scalar!(vmstate_info_uint16, u16, VMS_VARRAY_UINT16);
impl_vmstate_scalar!(vmstate_info_uint32, u32, VMS_VARRAY_UINT32);
impl_vmstate_scalar!(vmstate_info_uint64, u64);
impl_vmstate_scalar!(vmstate_info_timer, util::timer::Timer);

#[macro_export]
macro_rules! impl_vmstate_c_struct {
    ($type:ty, $vmsd:expr) => {
        unsafe impl $crate::vmstate::VMState for $type {
            const BASE: $crate::bindings::VMStateField = $crate::bindings::VMStateField {
                vmsd: ::std::ptr::addr_of!($vmsd),
                size: ::std::mem::size_of::<$type>(),
                flags: $crate::bindings::VMStateFlags::VMS_STRUCT,
                ..::common::zeroable::Zeroable::ZERO
            };
        }
    };
}

// Pointer types using the underlying type's VMState plus VMS_POINTER
// Note that references are not supported, though references to cells
// could be allowed.

#[macro_export]
macro_rules! impl_vmstate_pointer {
    ($type:ty where $base:tt: VMState $($where:tt)*) => {
        unsafe impl<$base> $crate::vmstate::VMState for $type where $base: $crate::vmstate::VMState $($where)* {
            const BASE: $crate::vmstate::VMStateField = <$base as $crate::vmstate::VMState>::BASE.with_pointer_flag();
        }
    };
}

impl_vmstate_pointer!(*const T where T: VMState);
impl_vmstate_pointer!(*mut T where T: VMState);
impl_vmstate_pointer!(NonNull<T> where T: VMState);

// Unlike C pointers, Box is always non-null therefore there is no need
// to specify VMS_ALLOC.
impl_vmstate_pointer!(Box<T> where T: VMState);

// Arrays using the underlying type's VMState plus
// VMS_ARRAY/VMS_ARRAY_OF_POINTER

unsafe impl<T: VMState, const N: usize> VMState for [T; N] {
    const BASE: VMStateField = <T as VMState>::BASE.with_array_flag(N);
}

#[doc(alias = "VMSTATE_UNUSED")]
#[macro_export]
macro_rules! vmstate_unused {
    ($size:expr) => {{
        $crate::bindings::VMStateField {
            name: c"unused".as_ptr(),
            size: $size,
            info: unsafe { ::core::ptr::addr_of!($crate::bindings::vmstate_info_unused_buffer) },
            flags: $crate::bindings::VMStateFlags::VMS_BUFFER,
            ..::common::Zeroable::ZERO
        }
    }};
}

pub extern "C" fn rust_vms_test_field_exists<T, F: for<'a> FnCall<(&'a T, u8), bool>>(
    opaque: *mut c_void,
    version_id: c_int,
) -> bool {
    // SAFETY: the function is used in T's implementation of VMState
    let owner: &T = unsafe { &*(opaque.cast::<T>()) };
    let version: u8 = version_id.try_into().unwrap();
    F::call((owner, version))
}

pub type VMSFieldExistCb = unsafe extern "C" fn(
    opaque: *mut std::os::raw::c_void,
    version_id: std::os::raw::c_int,
) -> bool;

#[macro_export]
macro_rules! vmstate_exist_fn {
    ($struct_name:ty, $test_fn:expr) => {{
        const fn test_cb_builder__<T, F: for<'a> ::common::FnCall<(&'a T, u8), bool>>(
            _phantom: ::core::marker::PhantomData<F>,
        ) -> $crate::vmstate::VMSFieldExistCb {
            const { assert!(F::IS_SOME) };
            $crate::vmstate::rust_vms_test_field_exists::<T, F>
        }

        const fn phantom__<T>(_: &T) -> ::core::marker::PhantomData<T> {
            ::core::marker::PhantomData
        }
        Some(test_cb_builder__::<$struct_name, _>(phantom__(&$test_fn)))
    }};
}

/// Add a terminator to the fields in the arguments, and return
/// a reference to the resulting array of values.
#[macro_export]
macro_rules! vmstate_fields_ref {
    ($($field:expr),*$(,)*) => {
        &[
            $($field),*,
            $crate::bindings::VMStateField {
                flags: $crate::bindings::VMStateFlags::VMS_END,
                ..::common::zeroable::Zeroable::ZERO
            }
        ]
    }
}

/// Helper macro to declare a list of
/// ([`VMStateField`](`crate::bindings::VMStateField`)) into a static and return
/// a pointer to the array of values it created.
#[macro_export]
macro_rules! vmstate_fields {
    ($($field:expr),*$(,)*) => {{
        static _FIELDS: &[$crate::bindings::VMStateField] = $crate::vmstate_fields_ref!(
            $($field),*,
        );
        _FIELDS
    }}
}

#[doc(alias = "VMSTATE_VALIDATE")]
#[macro_export]
macro_rules! vmstate_validate {
    ($struct_name:ty, $test_name:expr, $test_fn:expr $(,)?) => {
        $crate::bindings::VMStateField {
            name: ::std::ffi::CStr::as_ptr($test_name),
            field_exists: $crate::vmstate_exist_fn!($struct_name, $test_fn),
            flags: $crate::bindings::VMStateFlags(
                $crate::bindings::VMStateFlags::VMS_MUST_EXIST.0
                    | $crate::bindings::VMStateFlags::VMS_ARRAY.0,
            ),
            num: 0, // 0 elements: no data, only run test_fn callback
            ..::common::zeroable::Zeroable::ZERO
        }
    };
}

/// Helper macro to allow using a struct in [`vmstate_of!`]
///
/// # Safety
///
/// The [`VMStateDescription`] constant `$vmsd` must be an accurate
/// description of the struct.
#[macro_export]
macro_rules! impl_vmstate_struct {
    ($type:ty, $vmsd:expr) => {
        unsafe impl $crate::vmstate::VMState for $type {
            const BASE: $crate::bindings::VMStateField = {
                static VMSD: &$crate::bindings::VMStateDescription = $vmsd.as_ref();

                $crate::bindings::VMStateField {
                    vmsd: ::core::ptr::addr_of!(*VMSD),
                    size: ::core::mem::size_of::<$type>(),
                    flags: $crate::bindings::VMStateFlags::VMS_STRUCT,
                    ..common::Zeroable::ZERO
                }
            };
        }
    };
}

/// The type returned by [`vmstate_subsections!`](crate::vmstate_subsections).
pub type VMStateSubsections = &'static [Option<&'static crate::bindings::VMStateDescription>];

/// Helper macro to declare a list of subsections ([`VMStateDescription`])
/// into a static and return a pointer to the array of pointers it created.
#[macro_export]
macro_rules! vmstate_subsections {
    ($($subsection:expr),*$(,)*) => {{
        static _SUBSECTIONS: $crate::vmstate::VMStateSubsections = &[
            $({
                static _SUBSECTION: $crate::bindings::VMStateDescription = $subsection.get();
                Some(&_SUBSECTION)
            }),*,
            None,
        ];
        &_SUBSECTIONS
    }}
}

pub struct VMStateDescription<T>(bindings::VMStateDescription, PhantomData<fn(&T)>);

// SAFETY: When a *const T is passed to the callbacks, the call itself
// is done in a thread-safe manner.  The invocation is okay as long as
// T itself is `Sync`.
unsafe impl<T: Sync> Sync for VMStateDescription<T> {}

#[derive(Clone)]
pub struct VMStateDescriptionBuilder<T>(bindings::VMStateDescription, PhantomData<fn(&T)>);

#[derive(Debug)]
pub struct InvalidError;

impl Error for InvalidError {}

impl std::fmt::Display for InvalidError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "invalid migration data")
    }
}

impl From<InvalidError> for Errno {
    fn from(_value: InvalidError) -> Errno {
        io::ErrorKind::InvalidInput.into()
    }
}

unsafe extern "C" fn vmstate_no_version_cb<
    T,
    F: for<'a> FnCall<(&'a T,), Result<(), impl Into<Errno>>>,
>(
    opaque: *mut c_void,
) -> c_int {
    // SAFETY: the function is used in T's implementation of VMState
    let result = F::call((unsafe { &*(opaque.cast::<T>()) },));
    into_neg_errno(result)
}

unsafe extern "C" fn vmstate_post_load_cb<
    T,
    F: for<'a> FnCall<(&'a T, u8), Result<(), impl Into<Errno>>>,
>(
    opaque: *mut c_void,
    version_id: c_int,
) -> c_int {
    // SAFETY: the function is used in T's implementation of VMState
    let owner: &T = unsafe { &*(opaque.cast::<T>()) };
    let version: u8 = version_id.try_into().unwrap();
    let result = F::call((owner, version));
    into_neg_errno(result)
}

unsafe extern "C" fn vmstate_needed_cb<T, F: for<'a> FnCall<(&'a T,), bool>>(
    opaque: *mut c_void,
) -> bool {
    // SAFETY: the function is used in T's implementation of VMState
    F::call((unsafe { &*(opaque.cast::<T>()) },))
}

unsafe extern "C" fn vmstate_dev_unplug_pending_cb<T, F: for<'a> FnCall<(&'a T,), bool>>(
    opaque: *mut c_void,
) -> bool {
    // SAFETY: the function is used in T's implementation of VMState
    F::call((unsafe { &*(opaque.cast::<T>()) },))
}

impl<T> VMStateDescriptionBuilder<T> {
    #[must_use]
    pub const fn name(mut self, name_str: &CStr) -> Self {
        self.0.name = ::std::ffi::CStr::as_ptr(name_str);
        self
    }

    #[must_use]
    pub const fn unmigratable(mut self) -> Self {
        self.0.unmigratable = true;
        self
    }

    #[must_use]
    pub const fn early_setup(mut self) -> Self {
        self.0.early_setup = true;
        self
    }

    #[must_use]
    pub const fn version_id(mut self, version: u8) -> Self {
        self.0.version_id = version as c_int;
        self
    }

    #[must_use]
    pub const fn minimum_version_id(mut self, min_version: u8) -> Self {
        self.0.minimum_version_id = min_version as c_int;
        self
    }

    #[must_use]
    pub const fn priority(mut self, priority: MigrationPriority) -> Self {
        self.0.priority = priority;
        self
    }

    #[must_use]
    pub const fn pre_load<F: for<'a> FnCall<(&'a T,), Result<(), impl Into<Errno>>>>(
        mut self,
        _f: &F,
    ) -> Self {
        self.0.pre_load = if F::IS_SOME {
            Some(vmstate_no_version_cb::<T, F>)
        } else {
            None
        };
        self
    }

    #[must_use]
    pub const fn post_load<F: for<'a> FnCall<(&'a T, u8), Result<(), impl Into<Errno>>>>(
        mut self,
        _f: &F,
    ) -> Self {
        self.0.post_load = if F::IS_SOME {
            Some(vmstate_post_load_cb::<T, F>)
        } else {
            None
        };
        self
    }

    #[must_use]
    pub const fn pre_save<F: for<'a> FnCall<(&'a T,), Result<(), impl Into<Errno>>>>(
        mut self,
        _f: &F,
    ) -> Self {
        self.0.pre_save = if F::IS_SOME {
            Some(vmstate_no_version_cb::<T, F>)
        } else {
            None
        };
        self
    }

    #[must_use]
    pub const fn post_save<F: for<'a> FnCall<(&'a T,), Result<(), impl Into<Errno>>>>(
        mut self,
        _f: &F,
    ) -> Self {
        self.0.post_save = if F::IS_SOME {
            Some(vmstate_no_version_cb::<T, F>)
        } else {
            None
        };
        self
    }

    #[must_use]
    pub const fn needed<F: for<'a> FnCall<(&'a T,), bool>>(mut self, _f: &F) -> Self {
        self.0.needed = if F::IS_SOME {
            Some(vmstate_needed_cb::<T, F>)
        } else {
            None
        };
        self
    }

    #[must_use]
    pub const fn unplug_pending<F: for<'a> FnCall<(&'a T,), bool>>(mut self, _f: &F) -> Self {
        self.0.dev_unplug_pending = if F::IS_SOME {
            Some(vmstate_dev_unplug_pending_cb::<T, F>)
        } else {
            None
        };
        self
    }

    #[must_use]
    pub const fn fields(mut self, fields: &'static [VMStateField]) -> Self {
        if fields[fields.len() - 1].flags.0 != VMStateFlags::VMS_END.0 {
            panic!("fields are not terminated, use vmstate_fields!");
        }
        self.0.fields = fields.as_ptr();
        self
    }

    #[must_use]
    pub const fn subsections(mut self, subs: &'static VMStateSubsections) -> Self {
        if subs[subs.len() - 1].is_some() {
            panic!("subsections are not terminated, use vmstate_subsections!");
        }
        let subs: *const Option<&bindings::VMStateDescription> = subs.as_ptr();
        self.0.subsections = subs.cast::<*const bindings::VMStateDescription>();
        self
    }

    #[must_use]
    pub const fn build(self) -> VMStateDescription<T> {
        VMStateDescription::<T>(self.0, PhantomData)
    }

    #[must_use]
    pub const fn new() -> Self {
        Self(bindings::VMStateDescription::ZERO, PhantomData)
    }
}

impl<T> Default for VMStateDescriptionBuilder<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> VMStateDescription<T> {
    pub const fn get(&self) -> bindings::VMStateDescription {
        self.0
    }

    pub const fn as_ref(&self) -> &bindings::VMStateDescription {
        &self.0
    }
}
