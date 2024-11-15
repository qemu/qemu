// SPDX-License-Identifier: MIT
//
// This file is based on library/core/src/cell.rs from
// Rust 1.82.0.
//
// Permission is hereby granted, free of charge, to any
// person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the
// Software without restriction, including without
// limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software
// is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice
// shall be included in all copies or substantial portions
// of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
// ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
// TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
// SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
// IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

//! BQL-protected mutable containers.
//!
//! Rust memory safety is based on this rule: Given an object `T`, it is only
//! possible to have one of the following:
//!
//! - Having several immutable references (`&T`) to the object (also known as
//!   **aliasing**).
//! - Having one mutable reference (`&mut T`) to the object (also known as
//!   **mutability**).
//!
//! This is enforced by the Rust compiler. However, there are situations where
//! this rule is not flexible enough. Sometimes it is required to have multiple
//! references to an object and yet mutate it. In particular, QEMU objects
//! usually have their pointer shared with the "outside world very early in
//! their lifetime", for example when they create their
//! [`MemoryRegion`s](crate::bindings::MemoryRegion).  Therefore, individual
//! parts of a  device must be made mutable in a controlled manner through the
//! use of cell types.
//!
//! This module provides a way to do so via the Big QEMU Lock.  While
//! [`BqlCell<T>`] is essentially the same single-threaded primitive that is
//! available in `std::cell`, the BQL allows it to be used from a multi-threaded
//! context and to share references across threads, while maintaining Rust's
//! safety guarantees.  For this reason, unlike its `std::cell` counterpart,
//! `BqlCell` implements the `Sync` trait.
//!
//! BQL checks are performed in debug builds but can be optimized away in
//! release builds, providing runtime safety during development with no overhead
//! in production.
//!
//! Warning: While `BqlCell` is similar to its `std::cell` counterpart, the two
//! are not interchangeable. Using `std::cell` types in QEMU device
//! implementations is usually incorrect and can lead to thread-safety issues.
//!
//! ## `BqlCell<T>`
//!
//! [`BqlCell<T>`] implements interior mutability by moving values in and out of
//! the cell. That is, an `&mut T` to the inner value can never be obtained as
//! long as the cell is shared. The value itself cannot be directly obtained
//! without copying it, cloning it, or replacing it with something else. This
//! type provides the following methods, all of which can be called only while
//! the BQL is held:
//!
//!  - For types that implement [`Copy`], the [`get`](BqlCell::get) method
//!    retrieves the current interior value by duplicating it.
//!  - For types that implement [`Default`], the [`take`](BqlCell::take) method
//!    replaces the current interior value with [`Default::default()`] and
//!    returns the replaced value.
//!  - All types have:
//!    - [`replace`](BqlCell::replace): replaces the current interior value and
//!      returns the replaced value.
//!    - [`set`](BqlCell::set): this method replaces the interior value,
//!      dropping the replaced value.

use std::{cell::UnsafeCell, cmp::Ordering, fmt, mem};

use crate::bindings;

// TODO: When building doctests do not include the actual BQL, because cargo
// does not know how to link them to libqemuutil.  This can be fixed by
// running rustdoc from "meson test" instead of relying on cargo.
pub fn bql_locked() -> bool {
    // SAFETY: the function does nothing but return a thread-local bool
    !cfg!(MESON) || unsafe { bindings::bql_locked() }
}

/// A mutable memory location that is protected by the Big QEMU Lock.
///
/// # Memory layout
///
/// `BqlCell<T>` has the same in-memory representation as its inner type `T`.
#[repr(transparent)]
pub struct BqlCell<T> {
    value: UnsafeCell<T>,
}

// SAFETY: Same as for std::sync::Mutex.  In the end this *is* a Mutex,
// except it is stored out-of-line
unsafe impl<T: Send> Send for BqlCell<T> {}
unsafe impl<T: Send> Sync for BqlCell<T> {}

impl<T: Copy> Clone for BqlCell<T> {
    #[inline]
    fn clone(&self) -> BqlCell<T> {
        BqlCell::new(self.get())
    }
}

impl<T: Default> Default for BqlCell<T> {
    /// Creates a `BqlCell<T>`, with the `Default` value for T.
    #[inline]
    fn default() -> BqlCell<T> {
        BqlCell::new(Default::default())
    }
}

impl<T: PartialEq + Copy> PartialEq for BqlCell<T> {
    #[inline]
    fn eq(&self, other: &BqlCell<T>) -> bool {
        self.get() == other.get()
    }
}

impl<T: Eq + Copy> Eq for BqlCell<T> {}

impl<T: PartialOrd + Copy> PartialOrd for BqlCell<T> {
    #[inline]
    fn partial_cmp(&self, other: &BqlCell<T>) -> Option<Ordering> {
        self.get().partial_cmp(&other.get())
    }
}

impl<T: Ord + Copy> Ord for BqlCell<T> {
    #[inline]
    fn cmp(&self, other: &BqlCell<T>) -> Ordering {
        self.get().cmp(&other.get())
    }
}

impl<T> From<T> for BqlCell<T> {
    /// Creates a new `BqlCell<T>` containing the given value.
    fn from(t: T) -> BqlCell<T> {
        BqlCell::new(t)
    }
}

impl<T: fmt::Debug + Copy> fmt::Debug for BqlCell<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.get().fmt(f)
    }
}

impl<T: fmt::Display + Copy> fmt::Display for BqlCell<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.get().fmt(f)
    }
}

impl<T> BqlCell<T> {
    /// Creates a new `BqlCell` containing the given value.
    ///
    /// # Examples
    ///
    /// ```
    /// use qemu_api::cell::BqlCell;
    ///
    /// let c = BqlCell::new(5);
    /// ```
    #[inline]
    pub const fn new(value: T) -> BqlCell<T> {
        BqlCell {
            value: UnsafeCell::new(value),
        }
    }

    /// Sets the contained value.
    ///
    /// # Examples
    ///
    /// ```
    /// use qemu_api::cell::BqlCell;
    ///
    /// let c = BqlCell::new(5);
    ///
    /// c.set(10);
    /// ```
    #[inline]
    pub fn set(&self, val: T) {
        self.replace(val);
    }

    /// Replaces the contained value with `val`, and returns the old contained
    /// value.
    ///
    /// # Examples
    ///
    /// ```
    /// use qemu_api::cell::BqlCell;
    ///
    /// let cell = BqlCell::new(5);
    /// assert_eq!(cell.get(), 5);
    /// assert_eq!(cell.replace(10), 5);
    /// assert_eq!(cell.get(), 10);
    /// ```
    #[inline]
    pub fn replace(&self, val: T) -> T {
        assert!(bql_locked());
        // SAFETY: This can cause data races if called from multiple threads,
        // but it won't happen as long as C code accesses the value
        // under BQL protection only.
        mem::replace(unsafe { &mut *self.value.get() }, val)
    }

    /// Unwraps the value, consuming the cell.
    ///
    /// # Examples
    ///
    /// ```
    /// use qemu_api::cell::BqlCell;
    ///
    /// let c = BqlCell::new(5);
    /// let five = c.into_inner();
    ///
    /// assert_eq!(five, 5);
    /// ```
    pub fn into_inner(self) -> T {
        assert!(bql_locked());
        self.value.into_inner()
    }
}

impl<T: Copy> BqlCell<T> {
    /// Returns a copy of the contained value.
    ///
    /// # Examples
    ///
    /// ```
    /// use qemu_api::cell::BqlCell;
    ///
    /// let c = BqlCell::new(5);
    ///
    /// let five = c.get();
    /// ```
    #[inline]
    pub fn get(&self) -> T {
        assert!(bql_locked());
        // SAFETY: This can cause data races if called from multiple threads,
        // but it won't happen as long as C code accesses the value
        // under BQL protection only.
        unsafe { *self.value.get() }
    }
}

impl<T> BqlCell<T> {
    /// Returns a raw pointer to the underlying data in this cell.
    ///
    /// # Examples
    ///
    /// ```
    /// use qemu_api::cell::BqlCell;
    ///
    /// let c = BqlCell::new(5);
    ///
    /// let ptr = c.as_ptr();
    /// ```
    #[inline]
    pub const fn as_ptr(&self) -> *mut T {
        self.value.get()
    }
}

impl<T: Default> BqlCell<T> {
    /// Takes the value of the cell, leaving `Default::default()` in its place.
    ///
    /// # Examples
    ///
    /// ```
    /// use qemu_api::cell::BqlCell;
    ///
    /// let c = BqlCell::new(5);
    /// let five = c.take();
    ///
    /// assert_eq!(five, 5);
    /// assert_eq!(c.into_inner(), 0);
    /// ```
    pub fn take(&self) -> T {
        self.replace(Default::default())
    }
}
