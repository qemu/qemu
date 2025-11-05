// SPDX-License-Identifier: GPL-2.0-or-later

//! Error propagation for QEMU Rust code
//!
//! This module contains [`Error`], the bridge between Rust errors and
//! [`Result`](std::result::Result)s and QEMU's C [`Error`](bindings::Error)
//! struct.
//!
//! For FFI code, [`Error`] provides functions to simplify conversion between
//! the Rust ([`Result<>`](std::result::Result)) and C (`Error**`) conventions:
//!
//! * [`ok_or_propagate`](crate::Error::ok_or_propagate),
//!   [`bool_or_propagate`](crate::Error::bool_or_propagate),
//!   [`ptr_or_propagate`](crate::Error::ptr_or_propagate) can be used to build
//!   a C return value while also propagating an error condition
//!
//! * [`with_errp`](crate::Error::with_errp) can be used to build a `Result`
//!
//! This module is most commonly used at the boundary between C and Rust code;
//! other code will usually access it through the
//! [`utils::Result`](crate::Result) type alias, and will use the
//! [`std::error::Error`] interface to let C errors participate in Rust's error
//! handling functionality.
//!
//! Rust code can also create use this module to create an error object that
//! will be passed up to C code, though in most cases this will be done
//! transparently through the `?` operator.  Errors can be constructed from a
//! simple error string, from an [`anyhow::Error`] to pass any other Rust error
//! type up to C code, or from a combination of the two.
//!
//! The third case, corresponding to [`Error::with_error`], is the only one that
//! requires mentioning [`utils::Error`](crate::Error) explicitly.  Similar
//! to how QEMU's C code handles errno values, the string and the
//! `anyhow::Error` object will be concatenated with `:` as the separator.

use std::{
    borrow::Cow,
    ffi::{c_char, c_int, c_void, CStr},
    fmt::{self, Display},
    ops::Deref,
    panic,
    ptr::{self, addr_of_mut},
};

use foreign::{prelude::*, OwnedPointer};

use crate::bindings;

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug)]
pub struct Error {
    cause: anyhow::Error,
    file: &'static str,
    line: u32,
}

impl Deref for Error {
    type Target = anyhow::Error;

    fn deref(&self) -> &Self::Target {
        &self.cause
    }
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Display::fmt(&format_args!("{:#}", self.cause), f)
    }
}

impl<E> From<E> for Error
where
    anyhow::Error: From<E>,
{
    #[track_caller]
    fn from(src: E) -> Self {
        Self::new(anyhow::Error::from(src))
    }
}

impl Error {
    /// Create a new error from an [`anyhow::Error`].
    ///
    /// This wraps the error with QEMU's location tracking information.
    /// Most code should use the `?` operator instead of calling this directly.
    #[track_caller]
    pub fn new(cause: anyhow::Error) -> Self {
        let location = panic::Location::caller();
        Self {
            cause,
            file: location.file(),
            line: location.line(),
        }
    }

    /// Create a new error from a string message.
    ///
    /// This is a convenience wrapper around [`Error::new`] for simple string
    /// errors. Most code should use the [`ensure!`](crate::ensure) macro
    /// instead of calling this directly.
    #[track_caller]
    pub fn msg(src: impl Into<Cow<'static, str>>) -> Self {
        Self::new(anyhow::Error::msg(src.into()))
    }

    #[track_caller]
    #[doc(hidden)]
    #[inline(always)]
    pub fn format(args: fmt::Arguments) -> Self {
        // anyhow::Error::msg will allocate anyway, might as well let fmt::format doit.
        let msg = fmt::format(args);
        Self::new(anyhow::Error::msg(msg))
    }

    /// Create a new error, prepending `msg` to the
    /// description of `cause`
    #[track_caller]
    pub fn with_error(msg: impl Into<Cow<'static, str>>, cause: impl Into<anyhow::Error>) -> Self {
        fn do_with_error(
            msg: Cow<'static, str>,
            cause: anyhow::Error,
            location: &'static panic::Location<'static>,
        ) -> Error {
            Error {
                cause: cause.context(msg),
                file: location.file(),
                line: location.line(),
            }
        }
        do_with_error(msg.into(), cause.into(), panic::Location::caller())
    }

    /// Consume a result, returning `false` if it is an error and
    /// `true` if it is successful.  The error is propagated into
    /// `errp` like the C API `error_propagate` would do.
    ///
    /// # Safety
    ///
    /// `errp` must be a valid argument to `error_propagate`;
    /// typically it is received from C code and need not be
    /// checked further at the Rust↔C boundary.
    pub unsafe fn bool_or_propagate(result: Result<()>, errp: *mut *mut bindings::Error) -> bool {
        // SAFETY: caller guarantees errp is valid
        unsafe { Self::ok_or_propagate(result, errp) }.is_some()
    }

    /// Consume a result, returning a `NULL` pointer if it is an error and
    /// a C representation of the contents if it is successful.  This is
    /// similar to the C API `error_propagate`, but it panics if `*errp`
    /// is not `NULL`.
    ///
    /// # Safety
    ///
    /// `errp` must be a valid argument to `error_propagate`;
    /// typically it is received from C code and need not be
    /// checked further at the Rust↔C boundary.
    ///
    /// See [`propagate`](Error::propagate) for more information.
    #[must_use]
    pub unsafe fn ptr_or_propagate<T: CloneToForeign>(
        result: Result<T>,
        errp: *mut *mut bindings::Error,
    ) -> *mut T::Foreign {
        // SAFETY: caller guarantees errp is valid
        unsafe { Self::ok_or_propagate(result, errp) }.clone_to_foreign_ptr()
    }

    /// Consume a result in the same way as `self.ok()`, but also propagate
    /// a possible error into `errp`.  This is similar to the C API
    /// `error_propagate`, but it panics if `*errp` is not `NULL`.
    ///
    /// # Safety
    ///
    /// `errp` must be a valid argument to `error_propagate`;
    /// typically it is received from C code and need not be
    /// checked further at the Rust↔C boundary.
    ///
    /// See [`propagate`](Error::propagate) for more information.
    pub unsafe fn ok_or_propagate<T>(
        result: Result<T>,
        errp: *mut *mut bindings::Error,
    ) -> Option<T> {
        result.map_err(|err| unsafe { err.propagate(errp) }).ok()
    }

    /// Equivalent of the C function `error_propagate`.  Fill `*errp`
    /// with the information container in `self` if `errp` is not NULL;
    /// then consume it.
    ///
    /// This is similar to the C API `error_propagate`, but it panics if
    /// `*errp` is not `NULL`.
    ///
    /// # Safety
    ///
    /// `errp` must be a valid argument to `error_propagate`; it can be
    /// `NULL` or it can point to any of:
    /// * `error_abort`
    /// * `error_fatal`
    /// * a local variable of (C) type `Error *`
    ///
    /// Typically `errp` is received from C code and need not be
    /// checked further at the Rust↔C boundary.
    pub unsafe fn propagate(self, errp: *mut *mut bindings::Error) {
        if errp.is_null() {
            return;
        }

        // SAFETY: caller guarantees that errp and *errp are valid
        unsafe {
            assert_eq!(*errp, ptr::null_mut());
            bindings::error_propagate(errp, self.clone_to_foreign_ptr());
        }
    }

    /// Pass a C `Error*` to the closure, and convert the result
    /// (either the return value of the closure, or the error)
    /// into a Rust `Result`.
    ///
    /// # Safety
    ///
    /// One exit from `f`, `c_error` must be unchanged or point to a
    /// valid C [`struct Error`](bindings::Error).
    pub unsafe fn with_errp<T, F: FnOnce(&mut *mut bindings::Error) -> T>(f: F) -> Result<T> {
        let mut c_error: *mut bindings::Error = ptr::null_mut();

        // SAFETY: guaranteed by the postcondition of `f`
        match (f(&mut c_error), unsafe { c_error.into_native() }) {
            (result, None) => Ok(result),
            (_, Some(err)) => Err(err),
        }
    }
}

/// Extension trait for `std::result::Result`, providing extra
/// methods when the error type can be converted into a QEMU
/// Error.
pub trait ResultExt {
    /// The success type `T` in `Result<T, E>`.
    type OkType;

    /// Report a fatal error and exit QEMU, or return the success value.
    /// Note that, unlike [`unwrap()`](std::result::Result::unwrap), this
    /// is not an abort and can be used for user errors.
    fn unwrap_fatal(self) -> Self::OkType;
}

impl<T, E> ResultExt for std::result::Result<T, E>
where
    Error: From<E>,
{
    type OkType = T;

    fn unwrap_fatal(self) -> T {
        // SAFETY: errp is valid
        self.map_err(|err| unsafe {
            Error::from(err).propagate(addr_of_mut!(bindings::error_fatal))
        })
        .unwrap()
    }
}

impl FreeForeign for Error {
    type Foreign = bindings::Error;

    unsafe fn free_foreign(p: *mut bindings::Error) {
        // SAFETY: caller guarantees p is valid
        unsafe {
            bindings::error_free(p);
        }
    }
}

impl CloneToForeign for Error {
    fn clone_to_foreign(&self) -> OwnedPointer<Self> {
        // SAFETY: all arguments are controlled by this function
        unsafe {
            let err: *mut c_void = libc::malloc(std::mem::size_of::<bindings::Error>());
            let err: &mut bindings::Error = &mut *err.cast();
            *err = bindings::Error {
                msg: format!("{self}").clone_to_foreign_ptr(),
                err_class: bindings::ERROR_CLASS_GENERIC_ERROR,
                src_len: self.file.len() as c_int,
                src: self.file.as_ptr().cast::<c_char>(),
                line: self.line as c_int,
                func: ptr::null_mut(),
                hint: ptr::null_mut(),
            };
            OwnedPointer::new(err)
        }
    }
}

impl FromForeign for Error {
    unsafe fn cloned_from_foreign(c_error: *const bindings::Error) -> Self {
        // SAFETY: caller guarantees c_error is valid
        unsafe {
            let error = &*c_error;
            let file = if error.src_len < 0 {
                // NUL-terminated
                CStr::from_ptr(error.src).to_str()
            } else {
                // Can become str::from_utf8 with Rust 1.87.0
                std::str::from_utf8(std::slice::from_raw_parts(
                    &*error.src.cast::<u8>(),
                    error.src_len as usize,
                ))
            };

            Error {
                cause: anyhow::Error::msg(String::cloned_from_foreign(error.msg)),
                file: file.unwrap(),
                line: error.line as u32,
            }
        }
    }
}

/// Ensure that a condition is true, returning an error if it is false.
///
/// This macro is similar to [`anyhow::ensure`] but returns a QEMU [`Result`].
/// If the condition evaluates to `false`, the macro returns early with an error
/// constructed from the provided message.
///
/// # Examples
///
/// ```
/// # use util::{ensure, Result};
/// # fn check_positive(x: i32) -> Result<()> {
/// ensure!(x > 0, "value must be positive");
/// #   Ok(())
/// # }
/// ```
///
/// ```
/// # use util::{ensure, Result};
/// # const MIN: i32 = 123;
/// # const MAX: i32 = 456;
/// # fn check_range(x: i32) -> Result<()> {
/// ensure!(
///     x >= MIN && x <= MAX,
///     "{} not between {} and {}",
///     x,
///     MIN,
///     MAX
/// );
/// #   Ok(())
/// # }
/// ```
#[macro_export]
macro_rules! ensure {
    ($cond:expr, $fmt:literal, $($arg:tt)*) => {
        if !$cond {
            let e = $crate::Error::format(format_args!($fmt, $($arg)*));
            return $crate::Result::Err(e);
        }
    };
    ($cond:expr, $err:expr $(,)?) => {
        if !$cond {
            let e = $crate::Error::msg($err);
            return $crate::Result::Err(e);
        }
    };
}

#[cfg(test)]
mod tests {
    use std::ffi::CStr;

    use anyhow::anyhow;
    use common::assert_match;
    use foreign::OwnedPointer;

    use super::*;

    #[track_caller]
    fn error_for_test(msg: &CStr) -> OwnedPointer<Error> {
        // SAFETY: all arguments are controlled by this function
        let location = panic::Location::caller();
        unsafe {
            let err: *mut c_void = libc::malloc(std::mem::size_of::<bindings::Error>());
            let err: &mut bindings::Error = &mut *err.cast();
            *err = bindings::Error {
                msg: msg.clone_to_foreign_ptr(),
                err_class: bindings::ERROR_CLASS_GENERIC_ERROR,
                src_len: location.file().len() as c_int,
                src: location.file().as_ptr().cast::<c_char>(),
                line: location.line() as c_int,
                func: ptr::null_mut(),
                hint: ptr::null_mut(),
            };
            OwnedPointer::new(err)
        }
    }

    unsafe fn error_get_pretty<'a>(local_err: *mut bindings::Error) -> &'a CStr {
        unsafe { CStr::from_ptr(bindings::error_get_pretty(local_err)) }
    }

    #[test]
    fn test_display() {
        assert_eq!(&*format!("{}", Error::msg("msg")), "msg");
        assert_eq!(&*format!("{}", Error::msg("msg".to_owned())), "msg");
        assert_eq!(&*format!("{}", Error::from(anyhow!("msg"))), "msg");

        assert_eq!(
            &*format!("{}", Error::with_error("msg", anyhow!("cause"))),
            "msg: cause"
        );
    }

    #[test]
    fn test_bool_or_propagate() {
        unsafe {
            let mut local_err: *mut bindings::Error = ptr::null_mut();

            assert!(Error::bool_or_propagate(Ok(()), &mut local_err));
            assert_eq!(local_err, ptr::null_mut());

            let my_err = Error::msg("msg");
            assert!(!Error::bool_or_propagate(Err(my_err), &mut local_err));
            assert_ne!(local_err, ptr::null_mut());
            assert_eq!(error_get_pretty(local_err), c"msg");
            bindings::error_free(local_err);
        }
    }

    #[test]
    fn test_ptr_or_propagate() {
        unsafe {
            let mut local_err: *mut bindings::Error = ptr::null_mut();

            let ret = Error::ptr_or_propagate(Ok("abc".to_owned()), &mut local_err);
            assert_eq!(String::from_foreign(ret), "abc");
            assert_eq!(local_err, ptr::null_mut());

            let my_err = Error::msg("msg");
            assert_eq!(
                Error::ptr_or_propagate(Err::<String, _>(my_err), &mut local_err),
                ptr::null_mut()
            );
            assert_ne!(local_err, ptr::null_mut());
            assert_eq!(error_get_pretty(local_err), c"msg");
            bindings::error_free(local_err);
        }
    }

    #[test]
    fn test_with_errp() {
        unsafe {
            let result = Error::with_errp(|_errp| true);
            assert_match!(result, Ok(true));

            let err = Error::with_errp(|errp| {
                *errp = error_for_test(c"msg").into_inner();
                false
            })
            .unwrap_err();
            assert_eq!(&*format!("{err}"), "msg");
        }
    }
}
