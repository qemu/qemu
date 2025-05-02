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

// bindgen does not derive Default here
#[allow(clippy::derivable_impls)]
impl Default for crate::bindings::VMStateFlags {
    fn default() -> Self {
        Self(0)
    }
}

unsafe impl Zeroable for crate::bindings::Property__bindgen_ty_1 {}
unsafe impl Zeroable for crate::bindings::Property {}
unsafe impl Zeroable for crate::bindings::VMStateFlags {}
unsafe impl Zeroable for crate::bindings::VMStateField {}
unsafe impl Zeroable for crate::bindings::VMStateDescription {}
unsafe impl Zeroable for crate::bindings::MemoryRegionOps__bindgen_ty_1 {}
unsafe impl Zeroable for crate::bindings::MemoryRegionOps__bindgen_ty_2 {}
unsafe impl Zeroable for crate::bindings::MemoryRegionOps {}
unsafe impl Zeroable for crate::bindings::MemTxAttrs {}
unsafe impl Zeroable for crate::bindings::CharBackend {}
