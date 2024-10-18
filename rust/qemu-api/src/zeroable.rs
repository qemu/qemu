// SPDX-License-Identifier: GPL-2.0-or-later

/// Encapsulates the requirement that
/// `MaybeUninit::<Self>::zeroed().assume_init()` does not cause
/// undefined behavior.
///
/// # Safety
///
/// Do not add this trait to a type unless all-zeroes is
/// a valid value for the type.  In particular, remember that raw
/// pointers can be zero, but references and `NonNull<T>` cannot
/// unless wrapped with `Option<>`.
pub unsafe trait Zeroable: Default {
    /// SAFETY: If the trait was added to a type, then by definition
    /// this is safe.
    const ZERO: Self = unsafe { ::core::mem::MaybeUninit::<Self>::zeroed().assume_init() };
}

unsafe impl Zeroable for crate::bindings::Property__bindgen_ty_1 {}
unsafe impl Zeroable for crate::bindings::Property {}
unsafe impl Zeroable for crate::bindings::VMStateDescription {}
unsafe impl Zeroable for crate::bindings::MemoryRegionOps__bindgen_ty_1 {}
unsafe impl Zeroable for crate::bindings::MemoryRegionOps__bindgen_ty_2 {}
