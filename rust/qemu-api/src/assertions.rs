// Copyright 2024, Red Hat Inc.
// Author(s): Paolo Bonzini <pbonzini@redhat.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#![doc(hidden)]
//! This module provides macros to check the equality of types and
//! the type of `struct` fields.  This can be useful to ensure that
//! types match the expectations of C code.
//!
//! Documentation is hidden because it only exposes macros, which
//! are exported directly from `qemu_api`.

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

    ($t:ty, $i:tt, $ti:ty, num = $num:ident) => {
        const _: () = {
            #[allow(unused)]
            fn assert_field_type(v: $t) {
                fn types_must_be_equal<T, U>(_: T)
                where
                    T: $crate::assertions::EqType<Itself = U>,
                {
                }
                let index: usize = v.$num.try_into().unwrap();
                types_must_be_equal::<_, &$ti>(&v.$i[index]);
            }
        };
    };
}

/// Assert that an expression matches a pattern.  This can also be
/// useful to compare enums that do not implement `Eq`.
///
/// # Examples
///
/// ```
/// # use qemu_api::assert_match;
/// // JoinHandle does not implement `Eq`, therefore the result
/// // does not either.
/// let result: Result<std::thread::JoinHandle<()>, u32> = Err(42);
/// assert_match!(result, Err(42));
/// ```
#[macro_export]
macro_rules! assert_match {
    ($a:expr, $b:pat) => {
        assert!(
            match $a {
                $b => true,
                _ => false,
            },
            "{} = {:?} does not match {}",
            stringify!($a),
            $a,
            stringify!($b)
        );
    };
}

/// Assert at compile time that an expression is true.  This is similar
/// to `const { assert!(...); }` but it works outside functions, as well as
/// on versions of Rust before 1.79.
///
/// # Examples
///
/// ```
/// # use qemu_api::static_assert;
/// static_assert!("abc".len() == 3);
/// ```
///
/// ```compile_fail
/// # use qemu_api::static_assert;
/// static_assert!("abc".len() == 2); // does not compile
/// ```
#[macro_export]
macro_rules! static_assert {
    ($x:expr) => {
        const _: () = assert!($x);
    };
}
