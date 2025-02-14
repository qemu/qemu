// SPDX-License-Identifier: GPL-2.0-or-later

//! Defines a trait for structs that can be safely initialized with zero bytes.

/// Encapsulates the requirement that
/// `MaybeUninit::<Self>::zeroed().assume_init()` does not cause undefined
/// behavior.  This trait in principle could be implemented as just:
///
/// ```
/// pub unsafe trait Zeroable: Default {
///     const ZERO: Self = unsafe { ::core::mem::MaybeUninit::<Self>::zeroed().assume_init() };
/// }
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

/// A macro that acts similarly to [`core::mem::zeroed()`], only is const
///
/// ## Safety
///
/// Similar to `core::mem::zeroed()`, except this zeroes padding bits. Zeroed
/// padding usually isn't relevant to safety, but might be if a C union is used.
///
/// Just like for `core::mem::zeroed()`, an all zero byte pattern might not
/// be a valid value for a type, as is the case for references `&T` and `&mut
/// T`. Reference types trigger a (denied by default) lint and cause immediate
/// undefined behavior if the lint is ignored
///
/// ```rust compile_fail
/// use const_zero::const_zero;
/// // error: any use of this value will cause an error
/// // note: `#[deny(const_err)]` on by default
/// const STR: &str = unsafe{const_zero!(&'static str)};
/// ```
///
/// `const_zero` does not work on unsized types:
///
/// ```rust compile_fail
/// use const_zero::const_zero;
/// // error[E0277]: the size for values of type `[u8]` cannot be known at compilation time
/// const BYTES: [u8] = unsafe{const_zero!([u8])};
/// ```
/// ## Differences with `core::mem::zeroed`
///
/// `const_zero` zeroes padding bits, while `core::mem::zeroed` doesn't
#[macro_export]
macro_rules! const_zero {
    // This macro to produce a type-generic zero constant is taken from the
    // const_zero crate (v0.1.1):
    //
    //     https://docs.rs/const-zero/latest/src/const_zero/lib.rs.html
    //
    // and used under MIT license
    ($type_:ty) => {{
        const TYPE_SIZE: ::core::primitive::usize = ::core::mem::size_of::<$type_>();
        union TypeAsBytes {
            bytes: [::core::primitive::u8; TYPE_SIZE],
            inner: ::core::mem::ManuallyDrop<$type_>,
        }
        const ZERO_BYTES: TypeAsBytes = TypeAsBytes {
            bytes: [0; TYPE_SIZE],
        };
        ::core::mem::ManuallyDrop::<$type_>::into_inner(ZERO_BYTES.inner)
    }};
}

/// A wrapper to implement the `Zeroable` trait through the `const_zero` macro.
#[macro_export]
macro_rules! impl_zeroable {
    ($type:ty) => {
        unsafe impl $crate::zeroable::Zeroable for $type {
            const ZERO: Self = unsafe { $crate::const_zero!($type) };
        }
    };
}

// bindgen does not derive Default here
#[allow(clippy::derivable_impls)]
impl Default for crate::bindings::VMStateFlags {
    fn default() -> Self {
        Self(0)
    }
}

impl_zeroable!(crate::bindings::Property__bindgen_ty_1);
impl_zeroable!(crate::bindings::Property);
impl_zeroable!(crate::bindings::VMStateFlags);
impl_zeroable!(crate::bindings::VMStateField);
impl_zeroable!(crate::bindings::VMStateDescription);
impl_zeroable!(crate::bindings::MemoryRegionOps__bindgen_ty_1);
impl_zeroable!(crate::bindings::MemoryRegionOps__bindgen_ty_2);
impl_zeroable!(crate::bindings::MemoryRegionOps);
impl_zeroable!(crate::bindings::MemTxAttrs);
impl_zeroable!(crate::bindings::CharBackend);
