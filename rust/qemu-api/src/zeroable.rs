// SPDX-License-Identifier: GPL-2.0-or-later

use std::ptr;

/// Encapsulates the requirement that
/// `MaybeUninit::<Self>::zeroed().assume_init()` does not cause undefined
/// behavior.  This trait in principle could be implemented as just:
///
/// ```
///     const ZERO: Self = unsafe {
///         ::core::mem::MaybeUninit::<$crate::bindings::Property>::zeroed().assume_init()
///     },
/// ```
///
/// The need for a manual implementation is only because `zeroed()` cannot
/// be used as a `const fn` prior to Rust 1.75.0. Once we can assume a new
/// enough version of the compiler, we could provide a `#[derive(Zeroable)]`
/// macro to check at compile-time that all struct fields are Zeroable, and
/// use the above blanket implementation of the `ZERO` constant.
///
/// # Safety
///
/// Because the implementation of `ZERO` is manual, it does not make
/// any assumption on the safety of `zeroed()`.  However, other users of the
/// trait could use it that way.  Do not add this trait to a type unless
/// all-zeroes is a valid value for the type.  In particular, remember that
/// raw pointers can be zero, but references and `NonNull<T>` cannot
pub unsafe trait Zeroable: Default {
    const ZERO: Self;
}

unsafe impl Zeroable for crate::bindings::Property__bindgen_ty_1 {
    const ZERO: Self = Self { i: 0 };
}

unsafe impl Zeroable for crate::bindings::Property {
    const ZERO: Self = Self {
        name: ptr::null(),
        info: ptr::null(),
        offset: 0,
        bitnr: 0,
        bitmask: 0,
        set_default: false,
        defval: Zeroable::ZERO,
        arrayoffset: 0,
        arrayinfo: ptr::null(),
        arrayfieldsize: 0,
        link_type: ptr::null(),
    };
}

unsafe impl Zeroable for crate::bindings::VMStateDescription {
    const ZERO: Self = Self {
        name: ptr::null(),
        unmigratable: false,
        early_setup: false,
        version_id: 0,
        minimum_version_id: 0,
        priority: crate::bindings::MigrationPriority::MIG_PRI_DEFAULT,
        pre_load: None,
        post_load: None,
        pre_save: None,
        post_save: None,
        needed: None,
        dev_unplug_pending: None,
        fields: ptr::null(),
        subsections: ptr::null(),
    };
}

unsafe impl Zeroable for crate::bindings::MemoryRegionOps__bindgen_ty_1 {
    const ZERO: Self = Self {
        min_access_size: 0,
        max_access_size: 0,
        unaligned: false,
        accepts: None,
    };
}

unsafe impl Zeroable for crate::bindings::MemoryRegionOps__bindgen_ty_2 {
    const ZERO: Self = Self {
        min_access_size: 0,
        max_access_size: 0,
        unaligned: false,
    };
}
