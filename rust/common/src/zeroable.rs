// SPDX-License-Identifier: GPL-2.0-or-later

//! Defines a trait for structs that can be safely initialized with zero bytes.

/// Encapsulates the requirement that
/// `MaybeUninit::<Self>::zeroed().assume_init()` does not cause undefined
/// behavior.
///
/// # Safety
///
/// Do not add this trait to a type unless all-zeroes is a valid value for the
/// type.  In particular, raw pointers can be zero, but references and
/// `NonNull<T>` cannot.
pub unsafe trait Zeroable: Default {
    /// Return a value of Self whose memory representation consists of all
    /// zeroes, with the possible exclusion of padding bytes.
    const ZERO: Self = unsafe { ::core::mem::MaybeUninit::<Self>::zeroed().assume_init() };
}
