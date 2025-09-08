// SPDX-License-Identifier: GPL-2.0-or-later

//! Utility functions to convert `errno` to and from
//! [`io::Error`]/[`io::Result`]
//!
//! QEMU C functions often have a "positive success/negative `errno`" calling
//! convention.  This module provides functions to portably convert an integer
//! into an [`io::Result`] and back.

use std::{
    convert::{self, TryFrom},
    io::{self, ErrorKind},
};

/// An `errno` value that can be converted into an [`io::Error`]
pub struct Errno(pub u16);

// On Unix, from_raw_os_error takes an errno value and OS errors
// are printed using strerror.  On Windows however it takes a
// GetLastError() value; therefore we need to convert errno values
// into io::Error by hand.  This is the same mapping that the
// standard library uses to retrieve the kind of OS errors
// (`std::sys::pal::unix::decode_error_kind`).
impl From<Errno> for ErrorKind {
    fn from(value: Errno) -> ErrorKind {
        use ErrorKind::*;
        let Errno(errno) = value;
        match i32::from(errno) {
            libc::EPERM | libc::EACCES => PermissionDenied,
            libc::ENOENT => NotFound,
            libc::EINTR => Interrupted,
            x if x == libc::EAGAIN || x == libc::EWOULDBLOCK => WouldBlock,
            libc::ENOMEM => OutOfMemory,
            libc::EEXIST => AlreadyExists,
            libc::EINVAL => InvalidInput,
            libc::EPIPE => BrokenPipe,
            libc::EADDRINUSE => AddrInUse,
            libc::EADDRNOTAVAIL => AddrNotAvailable,
            libc::ECONNABORTED => ConnectionAborted,
            libc::ECONNREFUSED => ConnectionRefused,
            libc::ECONNRESET => ConnectionReset,
            libc::ENOTCONN => NotConnected,
            libc::ENOTSUP => Unsupported,
            libc::ETIMEDOUT => TimedOut,
            _ => Other,
        }
    }
}

// This is used on Windows for all io::Errors, but also on Unix if the
// io::Error does not have a raw OS error.  This is the reversed
// mapping of the above; EIO is returned for unknown ErrorKinds.
impl From<io::ErrorKind> for Errno {
    fn from(value: io::ErrorKind) -> Errno {
        use ErrorKind::*;
        let errno = match value {
            // can be both EPERM or EACCES :( pick one
            PermissionDenied => libc::EPERM,
            NotFound => libc::ENOENT,
            Interrupted => libc::EINTR,
            WouldBlock => libc::EAGAIN,
            OutOfMemory => libc::ENOMEM,
            AlreadyExists => libc::EEXIST,
            InvalidInput => libc::EINVAL,
            BrokenPipe => libc::EPIPE,
            AddrInUse => libc::EADDRINUSE,
            AddrNotAvailable => libc::EADDRNOTAVAIL,
            ConnectionAborted => libc::ECONNABORTED,
            ConnectionRefused => libc::ECONNREFUSED,
            ConnectionReset => libc::ECONNRESET,
            NotConnected => libc::ENOTCONN,
            Unsupported => libc::ENOTSUP,
            TimedOut => libc::ETIMEDOUT,
            _ => libc::EIO,
        };
        Errno(errno as u16)
    }
}

impl From<Errno> for io::Error {
    #[cfg(unix)]
    fn from(value: Errno) -> io::Error {
        let Errno(errno) = value;
        io::Error::from_raw_os_error(errno.into())
    }

    #[cfg(windows)]
    fn from(value: Errno) -> io::Error {
        let error_kind: ErrorKind = value.into();
        error_kind.into()
    }
}

impl From<io::Error> for Errno {
    fn from(value: io::Error) -> Errno {
        if cfg!(unix) {
            if let Some(errno) = value.raw_os_error() {
                return Errno(u16::try_from(errno).unwrap());
            }
        }
        value.kind().into()
    }
}

impl From<convert::Infallible> for Errno {
    fn from(_value: convert::Infallible) -> Errno {
        panic!("unreachable")
    }
}

/// Internal traits; used to enable [`into_io_result`] and [`into_neg_errno`]
/// for the "right" set of types.
mod traits {
    use super::Errno;

    /// A signed type that can be converted into an
    /// [`io::Result`](std::io::Result)
    pub trait GetErrno {
        /// Unsigned variant of `Self`, used as the type for the `Ok` case.
        type Out;

        /// Return `Ok(self)` if positive, `Err(Errno(-self))` if negative
        fn into_errno_result(self) -> Result<Self::Out, Errno>;
    }

    /// A type that can be taken out of an [`io::Result`](std::io::Result) and
    /// converted into "positive success/negative `errno`" convention.
    pub trait MergeErrno {
        /// Signed variant of `Self`, used as the return type of
        /// [`into_neg_errno`](super::into_neg_errno).
        type Out: From<u16> + std::ops::Neg<Output = Self::Out>;

        /// Return `self`, asserting that it is in range
        fn map_ok(self) -> Self::Out;
    }

    macro_rules! get_errno {
        ($t:ty, $out:ty) => {
            impl GetErrno for $t {
                type Out = $out;
                fn into_errno_result(self) -> Result<Self::Out, Errno> {
                    match self {
                        0.. => Ok(self as $out),
                        -65535..=-1 => Err(Errno(-self as u16)),
                        _ => panic!("{self} is not a negative errno"),
                    }
                }
            }
        };
    }

    get_errno!(i32, u32);
    get_errno!(i64, u64);
    get_errno!(isize, usize);

    macro_rules! merge_errno {
        ($t:ty, $out:ty) => {
            impl MergeErrno for $t {
                type Out = $out;
                fn map_ok(self) -> Self::Out {
                    self.try_into().unwrap()
                }
            }
        };
    }

    merge_errno!(u8, i32);
    merge_errno!(u16, i32);
    merge_errno!(u32, i32);
    merge_errno!(u64, i64);

    impl MergeErrno for () {
        type Out = i32;
        fn map_ok(self) -> i32 {
            0
        }
    }
}

use traits::{GetErrno, MergeErrno};

/// Convert an integer value into a [`io::Result`].
///
/// Positive values are turned into an `Ok` result; negative values
/// are interpreted as negated `errno` and turned into an `Err`.
///
/// ```
/// # use common::errno::into_io_result;
/// # use std::io::ErrorKind;
/// let ok = into_io_result(1i32).unwrap();
/// assert_eq!(ok, 1u32);
///
/// let err = into_io_result(-1i32).unwrap_err(); // -EPERM
/// assert_eq!(err.kind(), ErrorKind::PermissionDenied);
/// ```
///
/// # Panics
///
/// Since the result is an unsigned integer, negative values must
/// be close to 0; values that are too far away are considered
/// likely overflows and will panic:
///
/// ```should_panic
/// # use common::errno::into_io_result;
/// # #[allow(dead_code)]
/// let err = into_io_result(-0x1234_5678i32); // panic
/// ```
pub fn into_io_result<T: GetErrno>(value: T) -> io::Result<T::Out> {
    value.into_errno_result().map_err(Into::into)
}

/// Convert a [`Result`] into an integer value, using negative `errno`
/// values to report errors.
///
/// ```
/// # use common::errno::into_neg_errno;
/// # use std::io::{self, ErrorKind};
/// let ok: io::Result<()> = Ok(());
/// assert_eq!(into_neg_errno(ok), 0);
///
/// let err: io::Result<()> = Err(ErrorKind::InvalidInput.into());
/// assert_eq!(into_neg_errno(err), -22); // -EINVAL
/// ```
///
/// Since this module also provides the ability to convert [`io::Error`]
/// to an `errno` value, [`io::Result`] is the most commonly used type
/// for the argument of this function:
///
/// # Panics
///
/// Since the result is a signed integer, integer `Ok` values must remain
/// positive:
///
/// ```should_panic
/// # use common::errno::into_neg_errno;
/// # use std::io;
/// let err: io::Result<u32> = Ok(0x8899_AABB);
/// into_neg_errno(err) // panic
/// # ;
/// ```
pub fn into_neg_errno<T: MergeErrno, E: Into<Errno>>(value: Result<T, E>) -> T::Out {
    match value {
        Ok(x) => x.map_ok(),
        Err(err) => -T::Out::from(err.into().0),
    }
}

#[cfg(test)]
mod tests {
    use std::io::ErrorKind;

    use super::*;
    use crate::assert_match;

    #[test]
    pub fn test_from_u8() {
        let ok: io::Result<_> = Ok(42u8);
        assert_eq!(into_neg_errno(ok), 42);

        let err: io::Result<u8> = Err(io::ErrorKind::PermissionDenied.into());
        assert_eq!(into_neg_errno(err), -1);

        if cfg!(unix) {
            let os_err: io::Result<u8> = Err(io::Error::from_raw_os_error(10));
            assert_eq!(into_neg_errno(os_err), -10);
        }
    }

    #[test]
    pub fn test_from_u16() {
        let ok: io::Result<_> = Ok(1234u16);
        assert_eq!(into_neg_errno(ok), 1234);

        let err: io::Result<u16> = Err(io::ErrorKind::PermissionDenied.into());
        assert_eq!(into_neg_errno(err), -1);

        if cfg!(unix) {
            let os_err: io::Result<u16> = Err(io::Error::from_raw_os_error(10));
            assert_eq!(into_neg_errno(os_err), -10);
        }
    }

    #[test]
    pub fn test_i32() {
        assert_match!(into_io_result(1234i32), Ok(1234));

        let err = into_io_result(-1i32).unwrap_err();
        #[cfg(unix)]
        assert_match!(err.raw_os_error(), Some(1));
        assert_match!(err.kind(), ErrorKind::PermissionDenied);
    }

    #[test]
    pub fn test_from_u32() {
        let ok: io::Result<_> = Ok(1234u32);
        assert_eq!(into_neg_errno(ok), 1234);

        let err: io::Result<u32> = Err(io::ErrorKind::PermissionDenied.into());
        assert_eq!(into_neg_errno(err), -1);

        if cfg!(unix) {
            let os_err: io::Result<u32> = Err(io::Error::from_raw_os_error(10));
            assert_eq!(into_neg_errno(os_err), -10);
        }
    }

    #[test]
    pub fn test_i64() {
        assert_match!(into_io_result(1234i64), Ok(1234));

        let err = into_io_result(-22i64).unwrap_err();
        #[cfg(unix)]
        assert_match!(err.raw_os_error(), Some(22));
        assert_match!(err.kind(), ErrorKind::InvalidInput);
    }

    #[test]
    pub fn test_from_u64() {
        let ok: io::Result<_> = Ok(1234u64);
        assert_eq!(into_neg_errno(ok), 1234);

        let err: io::Result<u64> = Err(io::ErrorKind::InvalidInput.into());
        assert_eq!(into_neg_errno(err), -22);

        if cfg!(unix) {
            let os_err: io::Result<u64> = Err(io::Error::from_raw_os_error(6));
            assert_eq!(into_neg_errno(os_err), -6);
        }
    }

    #[test]
    pub fn test_isize() {
        assert_match!(into_io_result(1234isize), Ok(1234));

        let err = into_io_result(-4isize).unwrap_err();
        #[cfg(unix)]
        assert_match!(err.raw_os_error(), Some(4));
        assert_match!(err.kind(), ErrorKind::Interrupted);
    }

    #[test]
    pub fn test_from_unit() {
        let ok: io::Result<_> = Ok(());
        assert_eq!(into_neg_errno(ok), 0);

        let err: io::Result<()> = Err(io::ErrorKind::OutOfMemory.into());
        assert_eq!(into_neg_errno(err), -12);

        if cfg!(unix) {
            let os_err: io::Result<()> = Err(io::Error::from_raw_os_error(2));
            assert_eq!(into_neg_errno(os_err), -2);
        }
    }
}
