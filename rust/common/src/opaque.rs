// SPDX-License-Identifier: MIT

//! ## Opaque wrappers
//!
//! The cell types from the previous section are useful at the boundaries
//! of code that requires interior mutability.  When writing glue code that
//! interacts directly with C structs, however, it is useful to operate
//! at a lower level.
//!
//! C functions often violate Rust's fundamental assumptions about memory
//! safety by modifying memory even if it is shared.  Furthermore, C structs
//! often start their life uninitialized and may be populated lazily.
//!
//! For this reason, this module provides the [`Opaque<T>`] type to opt out
//! of Rust's usual guarantees about the wrapped type. Access to the wrapped
//! value is always through raw pointers, obtained via methods like
//! [`as_mut_ptr()`](Opaque::as_mut_ptr) and [`as_ptr()`](Opaque::as_ptr). These
//! pointers can then be passed to C functions or dereferenced; both actions
//! require `unsafe` blocks, making it clear where safety guarantees must be
//! manually verified. For example
//!
//! ```ignore
//! unsafe {
//!     let state = Opaque::<MyStruct>::uninit();
//!     qemu_struct_init(state.as_mut_ptr());
//! }
//! ```
//!
//! [`Opaque<T>`] will usually be wrapped one level further, so that
//! bridge methods can be added to the wrapper:
//!
//! ```ignore
//! pub struct MyStruct(Opaque<bindings::MyStruct>);
//!
//! impl MyStruct {
//!     fn new() -> Pin<Box<MyStruct>> {
//!         let result = Box::pin(unsafe { Opaque::uninit() });
//!         unsafe { qemu_struct_init(result.as_mut_ptr()) };
//!         result
//!     }
//! }
//! ```
//!
//! This pattern of wrapping bindgen-generated types in [`Opaque<T>`] provides
//! several advantages:
//!
//! * The choice of traits to be implemented is not limited by the
//!   bindgen-generated code.  For example, [`Drop`] can be added without
//!   disabling [`Copy`] on the underlying bindgen type
//!
//! * [`Send`] and [`Sync`] implementations can be controlled by the wrapper
//!   type rather than being automatically derived from the C struct's layout
//!
//! * Methods can be implemented in a separate crate from the bindgen-generated
//!   bindings
//!
//! * [`Debug`](std::fmt::Debug) and [`Display`](std::fmt::Display)
//!   implementations can be customized to be more readable than the raw C
//!   struct representation
//!
//! The [`Opaque<T>`] type does not include BQL validation; it is possible to
//! assert in the code that the right lock is taken, to use it together
//! with a custom lock guard type, or to let C code take the lock, as
//! appropriate.  It is also possible to use it with non-thread-safe
//! types, since by default (unlike [`BqlCell`] and [`BqlRefCell`]
//! it is neither `Sync` nor `Send`.
//!
//! While [`Opaque<T>`] is necessary for C interop, it should be used sparingly
//! and only at FFI boundaries. For QEMU-specific types that need interior
//! mutability, prefer [`BqlCell`] or [`BqlRefCell`].
//!
//! [`BqlCell`]: ../../bql/cell/struct.BqlCell.html
//! [`BqlRefCell`]: ../../bql/cell/struct.BqlRefCell.html
use std::{cell::UnsafeCell, fmt, marker::PhantomPinned, mem::MaybeUninit, ptr::NonNull};

/// Stores an opaque value that is shared with C code.
///
/// Often, C structs can changed when calling a C function even if they are
/// behind a shared Rust reference, or they can be initialized lazily and have
/// invalid bit patterns (e.g. `3` for a [`bool`]).  This goes against Rust's
/// strict aliasing rules, which normally prevent mutation through shared
/// references.
///
/// Wrapping the struct with `Opaque<T>` ensures that the Rust compiler does not
/// assume the usual constraints that Rust structs require, and allows using
/// shared references on the Rust side.
///
/// `Opaque<T>` is `#[repr(transparent)]`, so that it matches the memory layout
/// of `T`.
#[repr(transparent)]
pub struct Opaque<T> {
    value: UnsafeCell<MaybeUninit<T>>,
    // PhantomPinned also allows multiple references to the `Opaque<T>`, i.e.
    // one `&mut Opaque<T>` can coexist with a `&mut T` or any number of `&T`;
    // see https://docs.rs/pinned-aliasable/latest/pinned_aliasable/.
    _pin: PhantomPinned,
}

impl<T> Opaque<T> {
    /// Creates a new shared reference from a C pointer
    ///
    /// # Safety
    ///
    /// The pointer must be valid, though it need not point to a valid value.
    pub unsafe fn from_raw<'a>(ptr: *mut T) -> &'a Self {
        let ptr = NonNull::new(ptr).unwrap().cast::<Self>();
        // SAFETY: Self is a transparent wrapper over T
        unsafe { ptr.as_ref() }
    }

    /// Creates a new opaque object with uninitialized contents.
    ///
    /// # Safety
    ///
    /// Ultimately the pointer to the returned value will be dereferenced
    /// in another `unsafe` block, for example when passing it to a C function,
    /// but the functions containing the dereference are usually safe.  The
    /// value returned from `uninit()` must be initialized and pinned before
    /// calling them.
    pub const unsafe fn uninit() -> Self {
        Self {
            value: UnsafeCell::new(MaybeUninit::uninit()),
            _pin: PhantomPinned,
        }
    }

    /// Creates a new opaque object with zeroed contents.
    ///
    /// # Safety
    ///
    /// Ultimately the pointer to the returned value will be dereferenced
    /// in another `unsafe` block, for example when passing it to a C function,
    /// but the functions containing the dereference are usually safe.  The
    /// value returned from `uninit()` must be pinned (and possibly initialized)
    /// before calling them.
    pub const unsafe fn zeroed() -> Self {
        Self {
            value: UnsafeCell::new(MaybeUninit::zeroed()),
            _pin: PhantomPinned,
        }
    }

    /// Returns a raw mutable pointer to the opaque data.
    pub const fn as_mut_ptr(&self) -> *mut T {
        UnsafeCell::get(&self.value).cast()
    }

    /// Returns a raw pointer to the opaque data.
    pub const fn as_ptr(&self) -> *const T {
        self.as_mut_ptr().cast_const()
    }

    /// Returns a raw pointer to the opaque data that can be passed to a
    /// C function as `void *`.
    pub const fn as_void_ptr(&self) -> *mut std::ffi::c_void {
        UnsafeCell::get(&self.value).cast()
    }

    /// Converts a raw pointer to the wrapped type.
    pub const fn raw_get(slot: *mut Self) -> *mut T {
        // Compare with Linux's raw_get method, which goes through an UnsafeCell
        // because it takes a *const Self instead.
        slot.cast()
    }
}

impl<T> fmt::Debug for Opaque<T> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let mut name: String = "Opaque<".to_string();
        name += std::any::type_name::<T>();
        name += ">";
        f.debug_tuple(&name).field(&self.as_ptr()).finish()
    }
}

impl<T: Default> Opaque<T> {
    /// Creates a new opaque object with default contents.
    ///
    /// # Safety
    ///
    /// Ultimately the pointer to the returned value will be dereferenced
    /// in another `unsafe` block, for example when passing it to a C function,
    /// but the functions containing the dereference are usually safe.  The
    /// value returned from `uninit()` must be pinned before calling them.
    pub unsafe fn new() -> Self {
        Self {
            value: UnsafeCell::new(MaybeUninit::new(T::default())),
            _pin: PhantomPinned,
        }
    }
}

/// Annotates [`Self`] as a transparent wrapper for another type.
///
/// Usually defined via the [`crate::Wrapper`] derive macro.
///
/// # Examples
///
/// ```
/// # use std::mem::ManuallyDrop;
/// # use common::opaque::Wrapper;
/// #[repr(transparent)]
/// pub struct Example {
///     inner: ManuallyDrop<String>,
/// }
///
/// unsafe impl Wrapper for Example {
///     type Wrapped = String;
/// }
/// ```
///
/// # Safety
///
/// `Self` must be a `#[repr(transparent)]` wrapper for the `Wrapped` type,
/// whether directly or indirectly.
///
/// # Methods
///
/// By convention, types that implement Wrapper also implement the following
/// methods:
///
/// ```ignore
/// pub const unsafe fn from_raw<'a>(value: *mut Self::Wrapped) -> &'a Self;
/// pub const unsafe fn as_mut_ptr(&self) -> *mut Self::Wrapped;
/// pub const unsafe fn as_ptr(&self) -> *const Self::Wrapped;
/// pub const unsafe fn raw_get(slot: *mut Self) -> *const Self::Wrapped;
/// ```
///
/// They are not defined here to allow them to be `const`.
pub unsafe trait Wrapper {
    type Wrapped;
}

unsafe impl<T> Wrapper for Opaque<T> {
    type Wrapped = T;
}
