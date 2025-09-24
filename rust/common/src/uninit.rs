//! Access fields of a [`MaybeUninit`]

use std::{
    mem::MaybeUninit,
    ops::{Deref, DerefMut},
};

pub struct MaybeUninitField<'a, T, U> {
    parent: &'a mut MaybeUninit<T>,
    child: *mut U,
}

impl<'a, T, U> MaybeUninitField<'a, T, U> {
    #[doc(hidden)]
    pub const fn new(parent: &'a mut MaybeUninit<T>, child: *mut U) -> Self {
        MaybeUninitField { parent, child }
    }

    /// Return a constant pointer to the containing object of the field.
    ///
    /// Because the `MaybeUninitField` remembers the containing object,
    /// it is possible to use it in foreign APIs that initialize the
    /// child.
    pub const fn parent(f: &Self) -> *const T {
        f.parent.as_ptr()
    }

    /// Return a mutable pointer to the containing object.
    ///
    /// Because the `MaybeUninitField` remembers the containing object,
    /// it is possible to use it in foreign APIs that initialize the
    /// child.
    pub const fn parent_mut(f: &mut Self) -> *mut T {
        f.parent.as_mut_ptr()
    }
}

impl<T, U> Deref for MaybeUninitField<'_, T, U> {
    type Target = MaybeUninit<U>;

    fn deref(&self) -> &MaybeUninit<U> {
        // SAFETY: self.child was obtained by dereferencing a valid mutable
        // reference; the content of the memory may be invalid or uninitialized
        // but MaybeUninit<_> makes no assumption on it
        unsafe { &*(self.child.cast()) }
    }
}

impl<T, U> DerefMut for MaybeUninitField<'_, T, U> {
    fn deref_mut(&mut self) -> &mut MaybeUninit<U> {
        // SAFETY: self.child was obtained by dereferencing a valid mutable
        // reference; the content of the memory may be invalid or uninitialized
        // but MaybeUninit<_> makes no assumption on it
        unsafe { &mut *(self.child.cast()) }
    }
}

/// ```
/// #[derive(Debug)]
/// struct S {
///     x: u32,
///     y: u32,
/// }
///
/// # use std::mem::MaybeUninit;
/// # use common::{assert_match, uninit_field_mut};
///
/// let mut s: MaybeUninit<S> = MaybeUninit::zeroed();
/// uninit_field_mut!(s, x).write(5);
/// let s = unsafe { s.assume_init() };
/// assert_match!(s, S { x: 5, y: 0 });
/// ```
#[macro_export]
macro_rules! uninit_field_mut {
    ($container:expr, $($field:tt)+) => {{
        let container__: &mut ::std::mem::MaybeUninit<_> = &mut $container;
        let container_ptr__ = container__.as_mut_ptr();

        // SAFETY: the container is not used directly, only through a MaybeUninit<>,
        // so the safety is delegated to the caller and to final invocation of
        // assume_init()
        let target__ = unsafe { std::ptr::addr_of_mut!((*container_ptr__).$($field)+) };
        $crate::uninit::MaybeUninitField::new(container__, target__)
    }};
}
