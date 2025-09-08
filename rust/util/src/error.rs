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
//! * [`err_or_else`](crate::Error::err_or_else) and
//!   [`err_or_unit`](crate::Error::err_or_unit) can be used to build a `Result`
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
    panic, ptr,
};

use foreign::{prelude::*, OwnedPointer};

use crate::bindings;

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug)]
pub struct Error {
    msg: Option<Cow<'static, str>>,
    /// Appends the print string of the error to the msg if not None
    cause: Option<anyhow::Error>,
    file: &'static str,
    line: u32,
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        self.cause.as_ref().map(AsRef::as_ref)
    }

    #[allow(deprecated)]
    fn description(&self) -> &str {
        self.msg
            .as_deref()
            .or_else(|| self.cause.as_deref().map(std::error::Error::description))
            .expect("no message nor cause?")
    }
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let mut prefix = "";
        if let Some(ref msg) = self.msg {
            write!(f, "{msg}")?;
            prefix = ": ";
        }
        if let Some(ref cause) = self.cause {
            write!(f, "{prefix}{cause}")?;
        } else if prefix.is_empty() {
            panic!("no message nor cause?");
        }
        Ok(())
    }
}

impl From<String> for Error {
    #[track_caller]
    fn from(msg: String) -> Self {
        let location = panic::Location::caller();
        Error {
            msg: Some(Cow::Owned(msg)),
            cause: None,
            file: location.file(),
            line: location.line(),
        }
    }
}

impl From<&'static str> for Error {
    #[track_caller]
    fn from(msg: &'static str) -> Self {
        let location = panic::Location::caller();
        Error {
            msg: Some(Cow::Borrowed(msg)),
            cause: None,
            file: location.file(),
            line: location.line(),
        }
    }
}

impl From<anyhow::Error> for Error {
    #[track_caller]
    fn from(error: anyhow::Error) -> Self {
        let location = panic::Location::caller();
        Error {
            msg: None,
            cause: Some(error),
            file: location.file(),
            line: location.line(),
        }
    }
}

impl Error {
    /// Create a new error, prepending `msg` to the
    /// description of `cause`
    #[track_caller]
    pub fn with_error(msg: impl Into<Cow<'static, str>>, cause: impl Into<anyhow::Error>) -> Self {
        let location = panic::Location::caller();
        Error {
            msg: Some(msg.into()),
            cause: Some(cause.into()),
            file: location.file(),
            line: location.line(),
        }
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

    /// Convert a C `Error*` into a Rust `Result`, using
    /// `Ok(())` if `c_error` is NULL.  Free the `Error*`.
    ///
    /// # Safety
    ///
    /// `c_error` must be `NULL` or valid; typically it was initialized
    /// with `ptr::null_mut()` and passed by reference to a C function.
    pub unsafe fn err_or_unit(c_error: *mut bindings::Error) -> Result<()> {
        // SAFETY: caller guarantees c_error is valid
        unsafe { Self::err_or_else(c_error, || ()) }
    }

    /// Convert a C `Error*` into a Rust `Result`, calling `f()` to
    /// obtain an `Ok` value if `c_error` is NULL.  Free the `Error*`.
    ///
    /// # Safety
    ///
    /// `c_error` must be `NULL` or point to a valid C [`struct
    /// Error`](bindings::Error); typically it was initialized with
    /// `ptr::null_mut()` and passed by reference to a C function.
    pub unsafe fn err_or_else<T, F: FnOnce() -> T>(
        c_error: *mut bindings::Error,
        f: F,
    ) -> Result<T> {
        // SAFETY: caller guarantees c_error is valid
        let err = unsafe { Option::<Self>::from_foreign(c_error) };
        match err {
            None => Ok(f()),
            Some(err) => Err(err),
        }
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
                msg: FromForeign::cloned_from_foreign(error.msg),
                cause: None,
                file: file.unwrap(),
                line: error.line as u32,
            }
        }
    }
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
    #[allow(deprecated)]
    fn test_description() {
        use std::error::Error;

        assert_eq!(super::Error::from("msg").description(), "msg");
        assert_eq!(super::Error::from("msg".to_owned()).description(), "msg");
    }

    #[test]
    fn test_display() {
        assert_eq!(&*format!("{}", Error::from("msg")), "msg");
        assert_eq!(&*format!("{}", Error::from("msg".to_owned())), "msg");
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

            let my_err = Error::from("msg");
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

            let my_err = Error::from("msg");
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
    fn test_err_or_unit() {
        unsafe {
            let result = Error::err_or_unit(ptr::null_mut());
            assert_match!(result, Ok(()));

            let err = error_for_test(c"msg");
            let err = Error::err_or_unit(err.into_inner()).unwrap_err();
            assert_eq!(&*format!("{err}"), "msg");
        }
    }
}
