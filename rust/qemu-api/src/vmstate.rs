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
//!   which is defined by all migrateable types.
//!
//! * [`impl_vmstate_forward`](crate::impl_vmstate_forward) and
//!   [`impl_vmstate_bitsized`](crate::impl_vmstate_bitsized), which help with
//!   the definition of the [`VMState`] trait (respectively for transparent
//!   structs and for `bilge`-defined types)
//!
//! * helper macros to declare a device model state struct, in particular
//!   [`vmstate_subsections`](crate::vmstate_subsections) and
//!   [`vmstate_fields`](crate::vmstate_fields).
//!
//! * direct equivalents to the C macros declared in
//!   `include/migration/vmstate.h`. These are not type-safe and only provide
//!   functionality that is missing from `vmstate_of!`.

use core::{marker::PhantomData, mem, ptr::NonNull};
use std::os::raw::{c_int, c_void};

pub use crate::bindings::{VMStateDescription, VMStateField};
use crate::{
    bindings::VMStateFlags, callbacks::FnCall, prelude::*, qom::Owned, zeroable::Zeroable,
};

/// This macro is used to call a function with a generic argument bound
/// to the type of a field.  The function must take a
/// [`PhantomData`]`<T>` argument; `T` is the type of
/// field `$field` in the `$typ` type.
///
/// # Examples
///
/// ```
/// # use qemu_api::call_func_with_field;
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
            const fn phantom__<T>(_: &T) -> ::core::marker::PhantomData<T> { ::core::marker::PhantomData }
            // Unreachable code is exempt from checks on uninitialized values.
            // Use that trick to infer the type of this PhantomData.
            break ::core::marker::PhantomData;
            break phantom__(&{ let value__: $typ; value__.$($field).+ });
        })
    };
}

/// Workaround for lack of `const_refs_static`: references to global variables
/// can be included in a `static`, but not in a `const`; unfortunately, this
/// is exactly what would go in the `VMStateField`'s `info` member.
///
/// This enum contains the contents of the `VMStateField`'s `info` member,
/// but as an `enum` instead of a pointer.
#[allow(non_camel_case_types)]
pub enum VMStateFieldType {
    null,
    vmstate_info_bool,
    vmstate_info_int8,
    vmstate_info_int16,
    vmstate_info_int32,
    vmstate_info_int64,
    vmstate_info_uint8,
    vmstate_info_uint16,
    vmstate_info_uint32,
    vmstate_info_uint64,
    vmstate_info_timer,
}

/// Workaround for lack of `const_refs_static`.  Converts a `VMStateFieldType`
/// to a `*const VMStateInfo`, for inclusion in a `VMStateField`.
#[macro_export]
macro_rules! info_enum_to_ref {
    ($e:expr) => {
        unsafe {
            match $e {
                $crate::vmstate::VMStateFieldType::null => ::core::ptr::null(),
                $crate::vmstate::VMStateFieldType::vmstate_info_bool => {
                    ::core::ptr::addr_of!($crate::bindings::vmstate_info_bool)
                }
                $crate::vmstate::VMStateFieldType::vmstate_info_int8 => {
                    ::core::ptr::addr_of!($crate::bindings::vmstate_info_int8)
                }
                $crate::vmstate::VMStateFieldType::vmstate_info_int16 => {
                    ::core::ptr::addr_of!($crate::bindings::vmstate_info_int16)
                }
                $crate::vmstate::VMStateFieldType::vmstate_info_int32 => {
                    ::core::ptr::addr_of!($crate::bindings::vmstate_info_int32)
                }
                $crate::vmstate::VMStateFieldType::vmstate_info_int64 => {
                    ::core::ptr::addr_of!($crate::bindings::vmstate_info_int64)
                }
                $crate::vmstate::VMStateFieldType::vmstate_info_uint8 => {
                    ::core::ptr::addr_of!($crate::bindings::vmstate_info_uint8)
                }
                $crate::vmstate::VMStateFieldType::vmstate_info_uint16 => {
                    ::core::ptr::addr_of!($crate::bindings::vmstate_info_uint16)
                }
                $crate::vmstate::VMStateFieldType::vmstate_info_uint32 => {
                    ::core::ptr::addr_of!($crate::bindings::vmstate_info_uint32)
                }
                $crate::vmstate::VMStateFieldType::vmstate_info_uint64 => {
                    ::core::ptr::addr_of!($crate::bindings::vmstate_info_uint64)
                }
                $crate::vmstate::VMStateFieldType::vmstate_info_timer => {
                    ::core::ptr::addr_of!($crate::bindings::vmstate_info_timer)
                }
            }
        }
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
    /// The `info` member of a `VMStateField` is a pointer and as such cannot
    /// yet be included in the [`BASE`](VMState::BASE) associated constant;
    /// this is only allowed by Rust 1.83.0 and newer.  For now, include the
    /// member as an enum which is stored in a separate constant.
    const SCALAR_TYPE: VMStateFieldType = VMStateFieldType::null;

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

/// Internal utility function to retrieve a type's `VMStateFieldType`;
/// used by [`vmstate_of!`](crate::vmstate_of).
pub const fn vmstate_scalar_type<T: VMState>(_: PhantomData<T>) -> VMStateFieldType {
    T::SCALAR_TYPE
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
///   [`BqlCell`], [`BqlRefCell`]
/// * a raw pointer to any of the above
/// * a `NonNull` pointer, a `Box` or an [`Owned`] for any of the above
/// * an array of any of the above
///
/// In order to support other types, the trait `VMState` must be implemented
/// for them.  The macros
/// [`impl_vmstate_bitsized!`](crate::impl_vmstate_bitsized)
/// and [`impl_vmstate_forward!`](crate::impl_vmstate_forward) help with this.
#[macro_export]
macro_rules! vmstate_of {
    ($struct_name:ty, $field_name:ident $([0 .. $num:ident $(* $factor:expr)?])? $(,)?) => {
        $crate::bindings::VMStateField {
            name: ::core::concat!(::core::stringify!($field_name), "\0")
                .as_bytes()
                .as_ptr() as *const ::std::os::raw::c_char,
            offset: $crate::offset_of!($struct_name, $field_name),
            $(num_offset: $crate::offset_of!($struct_name, $num),)?
            // The calls to `call_func_with_field!` are the magic that
            // computes most of the VMStateField from the type of the field.
            info: $crate::info_enum_to_ref!($crate::call_func_with_field!(
                $crate::vmstate::vmstate_scalar_type,
                $struct_name,
                $field_name
            )),
            ..$crate::call_func_with_field!(
                $crate::vmstate::vmstate_base,
                $struct_name,
                $field_name
            )$(.with_varray_flag($crate::call_func_with_field!(
                    $crate::vmstate::vmstate_varray_flag,
                    $struct_name,
                    $num))
               $(.with_varray_multiply($factor))?)?
        }
    };
}

impl VMStateFlags {
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
    pub const fn with_varray_flag_unchecked(mut self, flag: VMStateFlags) -> VMStateField {
        self.flags = VMStateFlags(self.flags.0 & !VMStateFlags::VMS_ARRAY.0);
        self.flags = VMStateFlags(self.flags.0 | flag.0);
        self.num = 0; // varray uses num_offset instead of num.
        self
    }

    #[must_use]
    #[allow(unused_mut)]
    pub const fn with_varray_flag(mut self, flag: VMStateFlags) -> VMStateField {
        assert!((self.flags.0 & VMStateFlags::VMS_ARRAY.0) != 0);
        self.with_varray_flag_unchecked(flag)
    }

    #[must_use]
    pub const fn with_varray_multiply(mut self, num: u32) -> VMStateField {
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
/// # use qemu_api::impl_vmstate_forward;
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
            const SCALAR_TYPE: $crate::vmstate::VMStateFieldType =
                $crate::call_func_with_field!($crate::vmstate::vmstate_scalar_type, $tuple, 0);
            const BASE: $crate::bindings::VMStateField =
                $crate::call_func_with_field!($crate::vmstate::vmstate_base, $tuple, 0);
        }
    };
}

// Transparent wrappers: just use the internal type

macro_rules! impl_vmstate_transparent {
    ($type:ty where $base:tt: VMState $($where:tt)*) => {
        unsafe impl<$base> VMState for $type where $base: VMState $($where)* {
            const SCALAR_TYPE: VMStateFieldType = <$base as VMState>::SCALAR_TYPE;
            const BASE: VMStateField = VMStateField {
                size: mem::size_of::<$type>(),
                ..<$base as VMState>::BASE
            };
            const VARRAY_FLAG: VMStateFlags = <$base as VMState>::VARRAY_FLAG;
        }
    };
}

impl_vmstate_transparent!(std::cell::Cell<T> where T: VMState);
impl_vmstate_transparent!(std::cell::UnsafeCell<T> where T: VMState);
impl_vmstate_transparent!(std::pin::Pin<T> where T: VMState);
impl_vmstate_transparent!(crate::cell::BqlCell<T> where T: VMState);
impl_vmstate_transparent!(crate::cell::BqlRefCell<T> where T: VMState);
impl_vmstate_transparent!(crate::cell::Opaque<T> where T: VMState);

#[macro_export]
macro_rules! impl_vmstate_bitsized {
    ($type:ty) => {
        unsafe impl $crate::vmstate::VMState for $type {
            const SCALAR_TYPE: $crate::vmstate::VMStateFieldType =
                                        <<<$type as ::bilge::prelude::Bitsized>::ArbitraryInt
                                          as ::bilge::prelude::Number>::UnderlyingType
                                         as $crate::vmstate::VMState>::SCALAR_TYPE;
            const BASE: $crate::bindings::VMStateField =
                                        <<<$type as ::bilge::prelude::Bitsized>::ArbitraryInt
                                          as ::bilge::prelude::Number>::UnderlyingType
                                         as $crate::vmstate::VMState>::BASE;
            const VARRAY_FLAG: $crate::bindings::VMStateFlags =
                                        <<<$type as ::bilge::prelude::Bitsized>::ArbitraryInt
                                          as ::bilge::prelude::Number>::UnderlyingType
                                         as $crate::vmstate::VMState>::VARRAY_FLAG;
        }
    };
}

// Scalar types using predefined VMStateInfos

macro_rules! impl_vmstate_scalar {
    ($info:ident, $type:ty$(, $varray_flag:ident)?) => {
        unsafe impl VMState for $type {
            const SCALAR_TYPE: VMStateFieldType = VMStateFieldType::$info;
            const BASE: VMStateField = VMStateField {
                size: mem::size_of::<$type>(),
                flags: VMStateFlags::VMS_SINGLE,
                ..Zeroable::ZERO
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
impl_vmstate_scalar!(vmstate_info_timer, crate::timer::Timer);

// Pointer types using the underlying type's VMState plus VMS_POINTER
// Note that references are not supported, though references to cells
// could be allowed.

macro_rules! impl_vmstate_pointer {
    ($type:ty where $base:tt: VMState $($where:tt)*) => {
        unsafe impl<$base> VMState for $type where $base: VMState $($where)* {
            const SCALAR_TYPE: VMStateFieldType = <T as VMState>::SCALAR_TYPE;
            const BASE: VMStateField = <$base as VMState>::BASE.with_pointer_flag();
        }
    };
}

impl_vmstate_pointer!(*const T where T: VMState);
impl_vmstate_pointer!(*mut T where T: VMState);
impl_vmstate_pointer!(NonNull<T> where T: VMState);

// Unlike C pointers, Box is always non-null therefore there is no need
// to specify VMS_ALLOC.
impl_vmstate_pointer!(Box<T> where T: VMState);
impl_vmstate_pointer!(Owned<T> where T: VMState + ObjectType);

// Arrays using the underlying type's VMState plus
// VMS_ARRAY/VMS_ARRAY_OF_POINTER

unsafe impl<T: VMState, const N: usize> VMState for [T; N] {
    const SCALAR_TYPE: VMStateFieldType = <T as VMState>::SCALAR_TYPE;
    const BASE: VMStateField = <T as VMState>::BASE.with_array_flag(N);
}

#[doc(alias = "VMSTATE_UNUSED")]
#[macro_export]
macro_rules! vmstate_unused {
    ($size:expr) => {{
        $crate::bindings::VMStateField {
            name: $crate::c_str!("unused").as_ptr(),
            size: $size,
            info: unsafe { ::core::ptr::addr_of!($crate::bindings::vmstate_info_unused_buffer) },
            flags: $crate::bindings::VMStateFlags::VMS_BUFFER,
            ..$crate::zeroable::Zeroable::ZERO
        }
    }};
}

// FIXME: including the `vmsd` field in a `const` is not possible without
// the const_refs_static feature (stabilized in Rust 1.83.0).  Without it,
// it is not possible to use VMS_STRUCT in a transparent manner using
// `vmstate_of!`.  While VMSTATE_CLOCK can at least try to be type-safe,
// VMSTATE_STRUCT includes $type only for documentation purposes; it
// is checked against $field_name and $struct_name, but not against $vmsd
// which is what really would matter.
#[doc(alias = "VMSTATE_STRUCT")]
#[macro_export]
macro_rules! vmstate_struct {
    ($struct_name:ty, $field_name:ident $([0 .. $num:ident $(* $factor:expr)?])?, $vmsd:expr, $type:ty $(,)?) => {
        $crate::bindings::VMStateField {
            name: ::core::concat!(::core::stringify!($field_name), "\0")
                .as_bytes()
                .as_ptr() as *const ::std::os::raw::c_char,
            $(num_offset: $crate::offset_of!($struct_name, $num),)?
            offset: {
                $crate::assert_field_type!($struct_name, $field_name, $type $(, num = $num)?);
                $crate::offset_of!($struct_name, $field_name)
            },
            size: ::core::mem::size_of::<$type>(),
            flags: $crate::bindings::VMStateFlags::VMS_STRUCT,
            vmsd: $vmsd,
            ..$crate::zeroable::Zeroable::ZERO
         } $(.with_varray_flag_unchecked(
                  $crate::call_func_with_field!(
                      $crate::vmstate::vmstate_varray_flag,
                      $struct_name,
                      $num
                  )
              )
           $(.with_varray_multiply($factor))?)?
    };
}

#[doc(alias = "VMSTATE_CLOCK")]
#[macro_export]
macro_rules! vmstate_clock {
    ($struct_name:ty, $field_name:ident) => {{
        $crate::bindings::VMStateField {
            name: ::core::concat!(::core::stringify!($field_name), "\0")
                .as_bytes()
                .as_ptr() as *const ::std::os::raw::c_char,
            offset: {
                $crate::assert_field_type!(
                    $struct_name,
                    $field_name,
                    $crate::qom::Owned<$crate::qdev::Clock>
                );
                $crate::offset_of!($struct_name, $field_name)
            },
            size: ::core::mem::size_of::<*const $crate::qdev::Clock>(),
            flags: $crate::bindings::VMStateFlags(
                $crate::bindings::VMStateFlags::VMS_STRUCT.0
                    | $crate::bindings::VMStateFlags::VMS_POINTER.0,
            ),
            vmsd: unsafe { ::core::ptr::addr_of!($crate::bindings::vmstate_clock) },
            ..$crate::zeroable::Zeroable::ZERO
        }
    }};
}

/// Helper macro to declare a list of
/// ([`VMStateField`](`crate::bindings::VMStateField`)) into a static and return
/// a pointer to the array of values it created.
#[macro_export]
macro_rules! vmstate_fields {
    ($($field:expr),*$(,)*) => {{
        static _FIELDS: &[$crate::bindings::VMStateField] = &[
            $($field),*,
            $crate::bindings::VMStateField {
                flags: $crate::bindings::VMStateFlags::VMS_END,
                ..$crate::zeroable::Zeroable::ZERO
            }
        ];
        _FIELDS.as_ptr()
    }}
}

pub extern "C" fn rust_vms_test_field_exists<T, F: for<'a> FnCall<(&'a T, u8), bool>>(
    opaque: *mut c_void,
    version_id: c_int,
) -> bool {
    let owner: &T = unsafe { &*(opaque.cast::<T>()) };
    let version: u8 = version_id.try_into().unwrap();
    // SAFETY: the opaque was passed as a reference to `T`.
    F::call((owner, version))
}

pub type VMSFieldExistCb = unsafe extern "C" fn(
    opaque: *mut std::os::raw::c_void,
    version_id: std::os::raw::c_int,
) -> bool;

#[doc(alias = "VMSTATE_VALIDATE")]
#[macro_export]
macro_rules! vmstate_validate {
    ($struct_name:ty, $test_name:expr, $test_fn:expr $(,)?) => {
        $crate::bindings::VMStateField {
            name: ::std::ffi::CStr::as_ptr($test_name),
            field_exists: {
                const fn test_cb_builder__<
                    T,
                    F: for<'a> $crate::callbacks::FnCall<(&'a T, u8), bool>,
                >(
                    _phantom: ::core::marker::PhantomData<F>,
                ) -> $crate::vmstate::VMSFieldExistCb {
                    let _: () = F::ASSERT_IS_SOME;
                    $crate::vmstate::rust_vms_test_field_exists::<T, F>
                }

                const fn phantom__<T>(_: &T) -> ::core::marker::PhantomData<T> {
                    ::core::marker::PhantomData
                }
                Some(test_cb_builder__::<$struct_name, _>(phantom__(&$test_fn)))
            },
            flags: $crate::bindings::VMStateFlags(
                $crate::bindings::VMStateFlags::VMS_MUST_EXIST.0
                    | $crate::bindings::VMStateFlags::VMS_ARRAY.0,
            ),
            num: 0, // 0 elements: no data, only run test_fn callback
            ..$crate::zeroable::Zeroable::ZERO
        }
    };
}

/// A transparent wrapper type for the `subsections` field of
/// [`VMStateDescription`].
///
/// This is necessary to be able to declare subsection descriptions as statics,
/// because the only way to implement `Sync` for a foreign type (and `*const`
/// pointers are foreign types in Rust) is to create a wrapper struct and
/// `unsafe impl Sync` for it.
///
/// This struct is used in the
/// [`vm_state_subsections`](crate::vmstate_subsections) macro implementation.
#[repr(transparent)]
pub struct VMStateSubsectionsWrapper(pub &'static [*const crate::bindings::VMStateDescription]);

unsafe impl Sync for VMStateSubsectionsWrapper {}

/// Helper macro to declare a list of subsections ([`VMStateDescription`])
/// into a static and return a pointer to the array of pointers it created.
#[macro_export]
macro_rules! vmstate_subsections {
    ($($subsection:expr),*$(,)*) => {{
        static _SUBSECTIONS: $crate::vmstate::VMStateSubsectionsWrapper = $crate::vmstate::VMStateSubsectionsWrapper(&[
            $({
                static _SUBSECTION: $crate::bindings::VMStateDescription = $subsection;
                ::core::ptr::addr_of!(_SUBSECTION)
            }),*,
            ::core::ptr::null()
        ]);
        _SUBSECTIONS.0.as_ptr()
    }}
}
