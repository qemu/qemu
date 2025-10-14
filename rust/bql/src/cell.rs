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

//! QEMU-specific mutable containers
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
//! parts of a  device must be made mutable in a controlled manner; this module
//! provides the tools to do so.
//!
//! ## Cell types
//!
//! [`BqlCell<T>`] and [`BqlRefCell<T>`] allow doing this via the Big QEMU Lock.
//! While they are essentially the same single-threaded primitives that are
//! available in `std::cell`, the BQL allows them to be used from a
//! multi-threaded context and to share references across threads, while
//! maintaining Rust's safety guarantees.  For this reason, unlike
//! their `std::cell` counterparts, `BqlCell` and `BqlRefCell` implement the
//! `Sync` trait.
//!
//! BQL checks are performed in debug builds but can be optimized away in
//! release builds, providing runtime safety during development with no overhead
//! in production.
//!
//! The two provide different ways of handling interior mutability.
//! `BqlRefCell` is best suited for data that is primarily accessed by the
//! device's own methods, where multiple reads and writes can be grouped within
//! a single borrow and a mutable reference can be passed around.  Instead,
//! [`BqlCell`] is a better choice when sharing small pieces of data with
//! external code (especially C code), because it provides simple get/set
//! operations that can be used one at a time.
//!
//! Warning: While `BqlCell` and `BqlRefCell` are similar to their `std::cell`
//! counterparts, they are not interchangeable. Using `std::cell` types in
//! QEMU device implementations is usually incorrect and can lead to
//! thread-safety issues.
//!
//! ### Example
//!
//! ```ignore
//! # use bql::BqlRefCell;
//! # use qom::{Owned, ParentField};
//! # use system::{InterruptSource, IRQState, SysBusDevice};
//! # const N_GPIOS: usize = 8;
//! # struct PL061Registers { /* ... */ }
//! # unsafe impl ObjectType for PL061State {
//! #     type Class = <SysBusDevice as ObjectType>::Class;
//! #     const TYPE_NAME: &'static std::ffi::CStr = c"pl061";
//! # }
//! struct PL061State {
//!     parent_obj: ParentField<SysBusDevice>,
//!
//!     // Configuration is read-only after initialization
//!     pullups: u32,
//!     pulldowns: u32,
//!
//!     // Single values shared with C code use BqlCell, in this case via InterruptSource
//!     out: [InterruptSource; N_GPIOS],
//!     interrupt: InterruptSource,
//!
//!     // Larger state accessed by device methods uses BqlRefCell or Mutex
//!     registers: BqlRefCell<PL061Registers>,
//! }
//! ```
//!
//! ### `BqlCell<T>`
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
//!
//! ### `BqlRefCell<T>`
//!
//! [`BqlRefCell<T>`] uses Rust's lifetimes to implement "dynamic borrowing", a
//! process whereby one can claim temporary, exclusive, mutable access to the
//! inner value:
//!
//! ```ignore
//! fn clear_interrupts(&self, val: u32) {
//!     // A mutable borrow gives read-write access to the registers
//!     let mut regs = self.registers.borrow_mut();
//!     let old = regs.interrupt_status();
//!     regs.update_interrupt_status(old & !val);
//! }
//! ```
//!
//! Borrows for `BqlRefCell<T>`s are tracked at _runtime_, unlike Rust's native
//! reference types which are entirely tracked statically, at compile time.
//! Multiple immutable borrows are allowed via [`borrow`](BqlRefCell::borrow),
//! or a single mutable borrow via [`borrow_mut`](BqlRefCell::borrow_mut).  The
//! thread will panic if these rules are violated or if the BQL is not held.
use std::{
    cell::{Cell, UnsafeCell},
    cmp::Ordering,
    fmt,
    marker::PhantomData,
    mem,
    ops::{Deref, DerefMut},
    ptr::NonNull,
};

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
    /// use bql::BqlCell;
    /// # bql::start_test();
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
    /// use bql::BqlCell;
    /// # bql::start_test();
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
    /// use bql::BqlCell;
    /// # bql::start_test();
    ///
    /// let cell = BqlCell::new(5);
    /// assert_eq!(cell.get(), 5);
    /// assert_eq!(cell.replace(10), 5);
    /// assert_eq!(cell.get(), 10);
    /// ```
    #[inline]
    pub fn replace(&self, val: T) -> T {
        assert!(crate::is_locked());
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
    /// use bql::BqlCell;
    /// # bql::start_test();
    ///
    /// let c = BqlCell::new(5);
    /// let five = c.into_inner();
    ///
    /// assert_eq!(five, 5);
    /// ```
    pub fn into_inner(self) -> T {
        assert!(crate::is_locked());
        self.value.into_inner()
    }
}

impl<T: Copy> BqlCell<T> {
    /// Returns a copy of the contained value.
    ///
    /// # Examples
    ///
    /// ```
    /// use bql::BqlCell;
    /// # bql::start_test();
    ///
    /// let c = BqlCell::new(5);
    ///
    /// let five = c.get();
    /// ```
    #[inline]
    pub fn get(&self) -> T {
        assert!(crate::is_locked());
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
    /// use bql::BqlCell;
    /// # bql::start_test();
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
    /// use bql::BqlCell;
    /// # bql::start_test();
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

/// A mutable memory location with dynamically checked borrow rules,
/// protected by the Big QEMU Lock.
///
/// See the [module-level documentation](self) for more.
///
/// # Memory layout
///
/// `BqlRefCell<T>` starts with the same in-memory representation as its
/// inner type `T`.
#[repr(C)]
pub struct BqlRefCell<T> {
    // It is important that this is the first field (which is not the case
    // for std::cell::BqlRefCell), so that we can use offset_of! on it.
    // UnsafeCell and repr(C) both prevent usage of niches.
    value: UnsafeCell<T>,
    borrow: Cell<BorrowFlag>,
    // Stores the location of the earliest currently active borrow.
    // This gets updated whenever we go from having zero borrows
    // to having a single borrow. When a borrow occurs, this gets included
    // in the panic message
    #[cfg(feature = "debug_cell")]
    borrowed_at: Cell<Option<&'static std::panic::Location<'static>>>,
}

// Positive values represent the number of `BqlRef` active. Negative values
// represent the number of `BqlRefMut` active. Right now QEMU's implementation
// does not allow to create `BqlRefMut`s that refer to distinct, nonoverlapping
// components of a `BqlRefCell` (e.g., different ranges of a slice).
//
// `BqlRef` and `BqlRefMut` are both two words in size, and so there will likely
// never be enough `BqlRef`s or `BqlRefMut`s in existence to overflow half of
// the `usize` range. Thus, a `BorrowFlag` will probably never overflow or
// underflow. However, this is not a guarantee, as a pathological program could
// repeatedly create and then mem::forget `BqlRef`s or `BqlRefMut`s. Thus, all
// code must explicitly check for overflow and underflow in order to avoid
// unsafety, or at least behave correctly in the event that overflow or
// underflow happens (e.g., see BorrowRef::new).
type BorrowFlag = isize;
const UNUSED: BorrowFlag = 0;

#[inline(always)]
const fn is_writing(x: BorrowFlag) -> bool {
    x < UNUSED
}

#[inline(always)]
const fn is_reading(x: BorrowFlag) -> bool {
    x > UNUSED
}

impl<T> BqlRefCell<T> {
    /// Creates a new `BqlRefCell` containing `value`.
    ///
    /// # Examples
    ///
    /// ```
    /// use bql::BqlRefCell;
    ///
    /// let c = BqlRefCell::new(5);
    /// ```
    #[inline]
    pub const fn new(value: T) -> BqlRefCell<T> {
        BqlRefCell {
            value: UnsafeCell::new(value),
            borrow: Cell::new(UNUSED),
            #[cfg(feature = "debug_cell")]
            borrowed_at: Cell::new(None),
        }
    }
}

// This ensures the panicking code is outlined from `borrow_mut` for
// `BqlRefCell`.
#[inline(never)]
#[cold]
#[cfg(feature = "debug_cell")]
fn panic_already_borrowed(source: &Cell<Option<&'static std::panic::Location<'static>>>) -> ! {
    // If a borrow occurred, then we must already have an outstanding borrow,
    // so `borrowed_at` will be `Some`
    panic!("already borrowed at {:?}", source.take().unwrap())
}

#[inline(never)]
#[cold]
#[cfg(not(feature = "debug_cell"))]
fn panic_already_borrowed() -> ! {
    panic!("already borrowed")
}

impl<T> BqlRefCell<T> {
    #[inline]
    #[allow(clippy::unused_self)]
    fn panic_already_borrowed(&self) -> ! {
        #[cfg(feature = "debug_cell")]
        {
            panic_already_borrowed(&self.borrowed_at)
        }
        #[cfg(not(feature = "debug_cell"))]
        {
            panic_already_borrowed()
        }
    }

    /// Immutably borrows the wrapped value.
    ///
    /// The borrow lasts until the returned `BqlRef` exits scope. Multiple
    /// immutable borrows can be taken out at the same time.
    ///
    /// # Panics
    ///
    /// Panics if the value is currently mutably borrowed.
    ///
    /// # Examples
    ///
    /// ```
    /// use bql::BqlRefCell;
    /// # bql::start_test();
    ///
    /// let c = BqlRefCell::new(5);
    ///
    /// let borrowed_five = c.borrow();
    /// let borrowed_five2 = c.borrow();
    /// ```
    ///
    /// An example of panic:
    ///
    /// ```should_panic
    /// use bql::BqlRefCell;
    /// # bql::start_test();
    ///
    /// let c = BqlRefCell::new(5);
    ///
    /// let m = c.borrow_mut();
    /// let b = c.borrow(); // this causes a panic
    /// ```
    #[inline]
    #[track_caller]
    pub fn borrow(&self) -> BqlRef<'_, T> {
        if let Some(b) = BorrowRef::new(&self.borrow) {
            // `borrowed_at` is always the *first* active borrow
            if b.borrow.get() == 1 {
                #[cfg(feature = "debug_cell")]
                self.borrowed_at.set(Some(std::panic::Location::caller()));
            }

            crate::block_unlock(true);

            // SAFETY: `BorrowRef` ensures that there is only immutable access
            // to the value while borrowed.
            let value = unsafe { NonNull::new_unchecked(self.value.get()) };
            BqlRef { value, borrow: b }
        } else {
            self.panic_already_borrowed()
        }
    }

    /// Mutably borrows the wrapped value.
    ///
    /// The borrow lasts until the returned `BqlRefMut` or all `BqlRefMut`s
    /// derived from it exit scope. The value cannot be borrowed while this
    /// borrow is active.
    ///
    /// # Panics
    ///
    /// Panics if the value is currently borrowed.
    ///
    /// # Examples
    ///
    /// ```
    /// use bql::BqlRefCell;
    /// # bql::start_test();
    ///
    /// let c = BqlRefCell::new("hello".to_owned());
    ///
    /// *c.borrow_mut() = "bonjour".to_owned();
    ///
    /// assert_eq!(&*c.borrow(), "bonjour");
    /// ```
    ///
    /// An example of panic:
    ///
    /// ```should_panic
    /// use bql::BqlRefCell;
    /// # bql::start_test();
    ///
    /// let c = BqlRefCell::new(5);
    /// let m = c.borrow();
    ///
    /// let b = c.borrow_mut(); // this causes a panic
    /// ```
    #[inline]
    #[track_caller]
    pub fn borrow_mut(&self) -> BqlRefMut<'_, T> {
        if let Some(b) = BorrowRefMut::new(&self.borrow) {
            #[cfg(feature = "debug_cell")]
            {
                self.borrowed_at.set(Some(std::panic::Location::caller()));
            }

            // SAFETY: this only adjusts a counter
            crate::block_unlock(true);

            // SAFETY: `BorrowRefMut` guarantees unique access.
            let value = unsafe { NonNull::new_unchecked(self.value.get()) };
            BqlRefMut {
                value,
                _borrow: b,
                marker: PhantomData,
            }
        } else {
            self.panic_already_borrowed()
        }
    }

    /// Returns a mutable reference to the underlying data in this cell,
    /// while the owner already has a mutable reference to the cell.
    ///
    /// # Examples
    ///
    /// ```
    /// use bql::BqlRefCell;
    ///
    /// let mut c = BqlRefCell::new(5);
    ///
    /// *c.get_mut() = 10;
    /// ```
    #[inline]
    pub const fn get_mut(&mut self) -> &mut T {
        self.value.get_mut()
    }

    /// Returns a raw pointer to the underlying data in this cell.
    ///
    /// # Examples
    ///
    /// ```
    /// use bql::BqlRefCell;
    ///
    /// let c = BqlRefCell::new(5);
    ///
    /// let ptr = c.as_ptr();
    /// ```
    #[inline]
    pub const fn as_ptr(&self) -> *mut T {
        self.value.get()
    }
}

// SAFETY: Same as for std::sync::Mutex.  In the end this is a Mutex that is
// stored out-of-line.  Even though BqlRefCell includes Cells, they are
// themselves protected by the Big QEMU Lock.  Furtheremore, the Big QEMU
// Lock cannot be released while any borrows is active.
unsafe impl<T> Send for BqlRefCell<T> where T: Send {}
unsafe impl<T> Sync for BqlRefCell<T> {}

impl<T: Clone> Clone for BqlRefCell<T> {
    /// # Panics
    ///
    /// Panics if the value is currently mutably borrowed.
    #[inline]
    #[track_caller]
    fn clone(&self) -> BqlRefCell<T> {
        BqlRefCell::new(self.borrow().clone())
    }

    /// # Panics
    ///
    /// Panics if `source` is currently mutably borrowed.
    #[inline]
    #[track_caller]
    fn clone_from(&mut self, source: &Self) {
        self.value.get_mut().clone_from(&source.borrow())
    }
}

impl<T: Default> Default for BqlRefCell<T> {
    /// Creates a `BqlRefCell<T>`, with the `Default` value for T.
    #[inline]
    fn default() -> BqlRefCell<T> {
        BqlRefCell::new(Default::default())
    }
}

impl<T: PartialEq> PartialEq for BqlRefCell<T> {
    /// # Panics
    ///
    /// Panics if the value in either `BqlRefCell` is currently mutably
    /// borrowed.
    #[inline]
    fn eq(&self, other: &BqlRefCell<T>) -> bool {
        *self.borrow() == *other.borrow()
    }
}

impl<T: Eq> Eq for BqlRefCell<T> {}

impl<T: PartialOrd> PartialOrd for BqlRefCell<T> {
    /// # Panics
    ///
    /// Panics if the value in either `BqlRefCell` is currently mutably
    /// borrowed.
    #[inline]
    fn partial_cmp(&self, other: &BqlRefCell<T>) -> Option<Ordering> {
        self.borrow().partial_cmp(&*other.borrow())
    }
}

impl<T: Ord> Ord for BqlRefCell<T> {
    /// # Panics
    ///
    /// Panics if the value in either `BqlRefCell` is currently mutably
    /// borrowed.
    #[inline]
    fn cmp(&self, other: &BqlRefCell<T>) -> Ordering {
        self.borrow().cmp(&*other.borrow())
    }
}

impl<T> From<T> for BqlRefCell<T> {
    /// Creates a new `BqlRefCell<T>` containing the given value.
    fn from(t: T) -> BqlRefCell<T> {
        BqlRefCell::new(t)
    }
}

struct BorrowRef<'b> {
    borrow: &'b Cell<BorrowFlag>,
}

impl<'b> BorrowRef<'b> {
    #[inline]
    fn new(borrow: &'b Cell<BorrowFlag>) -> Option<BorrowRef<'b>> {
        let b = borrow.get().wrapping_add(1);
        if !is_reading(b) {
            // Incrementing borrow can result in a non-reading value (<= 0) in these cases:
            // 1. It was < 0, i.e. there are writing borrows, so we can't allow a read
            //    borrow due to Rust's reference aliasing rules
            // 2. It was isize::MAX (the max amount of reading borrows) and it overflowed
            //    into isize::MIN (the max amount of writing borrows) so we can't allow an
            //    additional read borrow because isize can't represent so many read borrows
            //    (this can only happen if you mem::forget more than a small constant amount
            //    of `BqlRef`s, which is not good practice)
            None
        } else {
            // Incrementing borrow can result in a reading value (> 0) in these cases:
            // 1. It was = 0, i.e. it wasn't borrowed, and we are taking the first read
            //    borrow
            // 2. It was > 0 and < isize::MAX, i.e. there were read borrows, and isize is
            //    large enough to represent having one more read borrow
            borrow.set(b);
            Some(BorrowRef { borrow })
        }
    }
}

impl Drop for BorrowRef<'_> {
    #[inline]
    fn drop(&mut self) {
        let borrow = self.borrow.get();
        debug_assert!(is_reading(borrow));
        self.borrow.set(borrow - 1);
        crate::block_unlock(false)
    }
}

impl Clone for BorrowRef<'_> {
    #[inline]
    fn clone(&self) -> Self {
        BorrowRef::new(self.borrow).unwrap()
    }
}

/// Wraps a borrowed reference to a value in a `BqlRefCell` box.
/// A wrapper type for an immutably borrowed value from a `BqlRefCell<T>`.
///
/// See the [module-level documentation](self) for more.
pub struct BqlRef<'b, T: 'b> {
    // NB: we use a pointer instead of `&'b T` to avoid `noalias` violations, because a
    // `BqlRef` argument doesn't hold immutability for its whole scope, only until it drops.
    // `NonNull` is also covariant over `T`, just like we would have with `&T`.
    value: NonNull<T>,
    borrow: BorrowRef<'b>,
}

impl<T> Deref for BqlRef<'_, T> {
    type Target = T;

    #[inline]
    fn deref(&self) -> &T {
        // SAFETY: the value is accessible as long as we hold our borrow.
        unsafe { self.value.as_ref() }
    }
}

impl<'b, T> BqlRef<'b, T> {
    /// Copies a `BqlRef`.
    ///
    /// The `BqlRefCell` is already immutably borrowed, so this cannot fail.
    ///
    /// This is an associated function that needs to be used as
    /// `BqlRef::clone(...)`. A `Clone` implementation or a method would
    /// interfere with the widespread use of `r.borrow().clone()` to clone
    /// the contents of a `BqlRefCell`.
    #[must_use]
    #[inline]
    #[allow(clippy::should_implement_trait)]
    pub fn clone(orig: &BqlRef<'b, T>) -> BqlRef<'b, T> {
        BqlRef {
            value: orig.value,
            borrow: orig.borrow.clone(),
        }
    }
}

impl<T: fmt::Debug> fmt::Debug for BqlRef<'_, T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        (**self).fmt(f)
    }
}

impl<T: fmt::Display> fmt::Display for BqlRef<'_, T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        (**self).fmt(f)
    }
}

struct BorrowRefMut<'b> {
    borrow: &'b Cell<BorrowFlag>,
}

impl<'b> BorrowRefMut<'b> {
    #[inline]
    fn new(borrow: &'b Cell<BorrowFlag>) -> Option<BorrowRefMut<'b>> {
        // There must currently be no existing references when borrow_mut() is
        // called, so we explicitly only allow going from UNUSED to UNUSED - 1.
        match borrow.get() {
            UNUSED => {
                borrow.set(UNUSED - 1);
                Some(BorrowRefMut { borrow })
            }
            _ => None,
        }
    }
}

impl Drop for BorrowRefMut<'_> {
    #[inline]
    fn drop(&mut self) {
        let borrow = self.borrow.get();
        debug_assert!(is_writing(borrow));
        self.borrow.set(borrow + 1);
        crate::block_unlock(false)
    }
}

/// A wrapper type for a mutably borrowed value from a `BqlRefCell<T>`.
///
/// See the [module-level documentation](self) for more.
pub struct BqlRefMut<'b, T: 'b> {
    // NB: we use a pointer instead of `&'b mut T` to avoid `noalias` violations, because a
    // `BqlRefMut` argument doesn't hold exclusivity for its whole scope, only until it drops.
    value: NonNull<T>,
    _borrow: BorrowRefMut<'b>,
    // `NonNull` is covariant over `T`, so we need to reintroduce invariance.
    marker: PhantomData<&'b mut T>,
}

impl<T> Deref for BqlRefMut<'_, T> {
    type Target = T;

    #[inline]
    fn deref(&self) -> &T {
        // SAFETY: the value is accessible as long as we hold our borrow.
        unsafe { self.value.as_ref() }
    }
}

impl<T> DerefMut for BqlRefMut<'_, T> {
    #[inline]
    fn deref_mut(&mut self) -> &mut T {
        // SAFETY: the value is accessible as long as we hold our borrow.
        unsafe { self.value.as_mut() }
    }
}

impl<T: fmt::Debug> fmt::Debug for BqlRefMut<'_, T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        (**self).fmt(f)
    }
}

impl<T: fmt::Display> fmt::Display for BqlRefMut<'_, T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        (**self).fmt(f)
    }
}
