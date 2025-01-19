// Copyright 2024, Red Hat Inc.
// Author(s): Paolo Bonzini <pbonzini@redhat.com>
// SPDX-License-Identifier: GPL-2.0-or-later

//! This module provides macros to check the equality of types and
//! the type of `struct` fields.  This can be useful to ensure that
//! types match the expectations of C code.

// Based on https://stackoverflow.com/questions/64251852/x/70978292#70978292
// (stackoverflow answers are released under MIT license).

#[doc(hidden)]
pub trait EqType {
    type Itself;
}

impl<T> EqType for T {
    type Itself = T;
}

/// Assert that two types are the same.
///
/// # Examples
///
/// ```
/// # use qemu_api::assert_same_type;
/// # use std::ops::Deref;
/// assert_same_type!(u32, u32);
/// assert_same_type!(<Box<u32> as Deref>::Target, u32);
/// ```
///
/// Different types will cause a compile failure
///
/// ```compile_fail
/// # use qemu_api::assert_same_type;
/// assert_same_type!(&Box<u32>, &u32);
/// ```
#[macro_export]
macro_rules! assert_same_type {
    ($t1:ty, $t2:ty) => {
        const _: () = {
            #[allow(unused)]
            fn assert_same_type(v: $t1) {
                fn types_must_be_equal<T, U>(_: T)
                where
                    T: $crate::assertions::EqType<Itself = U>,
                {
                }
                types_must_be_equal::<_, $t2>(v);
            }
        };
    };
}

/// Assert that a field of a struct has the given type.
///
/// # Examples
///
/// ```
/// # use qemu_api::assert_field_type;
/// pub struct A {
///     field1: u32,
/// }
///
/// assert_field_type!(A, field1, u32);
/// ```
///
/// Different types will cause a compile failure
///
/// ```compile_fail
/// # use qemu_api::assert_field_type;
/// # pub struct A { field1: u32 }
/// assert_field_type!(A, field1, i32);
/// ```
#[macro_export]
macro_rules! assert_field_type {
    ($t:ty, $i:tt, $ti:ty) => {
        const _: () = {
            #[allow(unused)]
            fn assert_field_type(v: $t) {
                fn types_must_be_equal<T, U>(_: T)
                where
                    T: $crate::assertions::EqType<Itself = U>,
                {
                }
                types_must_be_equal::<_, $ti>(v.$i);
            }
        };
    };
}
