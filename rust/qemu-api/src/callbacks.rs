// SPDX-License-Identifier: MIT

//! Utility functions to deal with callbacks from C to Rust.

use std::{mem, ptr::NonNull};

/// Trait for functions (types implementing [`Fn`]) that can be used as
/// callbacks. These include both zero-capture closures and function pointers.
///
/// In Rust, calling a function through the `Fn` trait normally requires a
/// `self` parameter, even though for zero-sized functions (including function
/// pointers) the type itself contains all necessary information to call the
/// function. This trait provides a `call` function that doesn't require `self`,
/// allowing zero-sized functions to be called using only their type.
///
/// This enables zero-sized functions to be passed entirely through generic
/// parameters and resolved at compile-time. A typical use is a function
/// receiving an unused parameter of generic type `F` and calling it via
/// `F::call` or passing it to another function via `func::<F>`.
///
/// QEMU uses this trick to create wrappers to C callbacks.  The wrappers
/// are needed to convert an opaque `*mut c_void` into a Rust reference,
/// but they only have a single opaque that they can use.  The `FnCall`
/// trait makes it possible to use that opaque for `self` or any other
/// reference:
///
/// ```ignore
/// // The compiler creates a new `rust_bh_cb` wrapper for each function
/// // passed to `qemu_bh_schedule_oneshot` below.
/// unsafe extern "C" fn rust_bh_cb<T, F: for<'a> FnCall<(&'a T,)>>(
///     opaque: *mut c_void,
/// ) {
///     // SAFETY: the opaque was passed as a reference to `T`.
///     F::call((unsafe { &*(opaque.cast::<T>()) }, ))
/// }
///
/// // The `_f` parameter is unused but it helps the compiler build the appropriate `F`.
/// // Using a reference allows usage in const context.
/// fn qemu_bh_schedule_oneshot<T, F: for<'a> FnCall<(&'a T,)>>(_f: &F, opaque: &T) {
///     let cb: unsafe extern "C" fn(*mut c_void) = rust_bh_cb::<T, F>;
///     unsafe {
///         bindings::qemu_bh_schedule_oneshot(cb, opaque as *const T as *const c_void as *mut c_void)
///     }
/// }
/// ```
///
/// Each wrapper is a separate instance of `rust_bh_cb` and is therefore
/// compiled to a separate function ("monomorphization").  If you wanted
/// to pass `self` as the opaque value, the generic parameters would be
/// `rust_bh_cb::<Self, F>`.
///
/// `Args` is a tuple type whose types are the arguments of the function,
/// while `R` is the returned type.
///
/// # Examples
///
/// ```
/// # use qemu_api::callbacks::FnCall;
/// fn call_it<F: for<'a> FnCall<(&'a str,), String>>(_f: &F, s: &str) -> String {
///     F::call((s,))
/// }
///
/// let s: String = call_it(&str::to_owned, "hello world");
/// assert_eq!(s, "hello world");
/// ```
///
/// Note that the compiler will produce a different version of `call_it` for
/// each function that is passed to it.  Therefore the argument is not really
/// used, except to decide what is `F` and what `F::call` does.
///
/// Attempting to pass a non-zero-sized closure causes a compile-time failure:
///
/// ```compile_fail
/// # use qemu_api::callbacks::FnCall;
/// # fn call_it<'a, F: FnCall<(&'a str,), String>>(_f: &F, s: &'a str) -> String {
/// #     F::call((s,))
/// # }
/// let x: &'static str = "goodbye world";
/// call_it(&move |_| String::from(x), "hello workd");
/// ```
///
/// # Safety
///
/// Because `Self` is a zero-sized type, all instances of the type are
/// equivalent. However, in addition to this, `Self` must have no invariants
/// that could be violated by creating a reference to it.
///
/// This is always true for zero-capture closures and function pointers, as long
/// as the code is able to name the function in the first place.
pub unsafe trait FnCall<Args, R = ()>: 'static + Sync + Sized {
    /// Referring to this internal constant asserts that the `Self` type is
    /// zero-sized.  Can be replaced by an inline const expression in
    /// Rust 1.79.0+.
    const ASSERT_ZERO_SIZED: () = { assert!(mem::size_of::<Self>() == 0) };

    /// Call the function with the arguments in args.
    fn call(a: Args) -> R;
}

macro_rules! impl_call {
    ($($args:ident,)* ) => (
        // SAFETY: because each function is treated as a separate type,
        // accessing `FnCall` is only possible in code that would be
        // allowed to call the function.
        unsafe impl<F, $($args,)* R> FnCall<($($args,)*), R> for F
        where
            F: 'static + Sync + Sized + Fn($($args, )*) -> R,
        {
            #[inline(always)]
            fn call(a: ($($args,)*)) -> R {
                let _: () = Self::ASSERT_ZERO_SIZED;

                // SAFETY: the safety of this method is the condition for implementing
                // `FnCall`.  As to the `NonNull` idiom to create a zero-sized type,
                // see https://github.com/rust-lang/libs-team/issues/292.
                let f: &'static F = unsafe { &*NonNull::<Self>::dangling().as_ptr() };
                let ($($args,)*) = a;
                f($($args,)*)
            }
        }
    )
}

impl_call!(_1, _2, _3, _4, _5,);
impl_call!(_1, _2, _3, _4,);
impl_call!(_1, _2, _3,);
impl_call!(_1, _2,);
impl_call!(_1,);
impl_call!();

#[cfg(test)]
mod tests {
    use super::*;

    // The `_f` parameter is unused but it helps the compiler infer `F`.
    fn do_test_call<'a, F: FnCall<(&'a str,), String>>(_f: &F) -> String {
        F::call(("hello world",))
    }

    #[test]
    fn test_call() {
        assert_eq!(do_test_call(&str::to_owned), "hello world")
    }
}
