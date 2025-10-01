// Copyright 2025 Bernhard Beschow <shentey@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Bindings for QEMU's logging infrastructure

use std::{
    io::{self, Write},
    ptr::NonNull,
};

use common::errno;

use crate::bindings;

#[repr(u32)]
/// Represents specific error categories within QEMU's logging system.
///
/// The `Log` enum provides a Rust abstraction for logging errors, corresponding
/// to a subset of the error categories defined in the C implementation.
pub enum Log {
    /// Log invalid access caused by the guest.
    /// Corresponds to `LOG_GUEST_ERROR` in the C implementation.
    GuestError = bindings::LOG_GUEST_ERROR,

    /// Log guest access of unimplemented functionality.
    /// Corresponds to `LOG_UNIMP` in the C implementation.
    Unimp = bindings::LOG_UNIMP,
}

/// A RAII guard for QEMU's logging infrastructure.  Creating the guard
/// locks the log file, and dropping it (letting it go out of scope) unlocks
/// the file.
///
/// As long as the guard lives, it can be written to using [`std::io::Write`].
///
/// The locking is recursive, therefore owning a guard does not prevent
/// using [`log_mask_ln!()`](crate::log_mask_ln).
pub struct LogGuard(NonNull<bindings::FILE>);

impl LogGuard {
    /// Return a RAII guard that writes to QEMU's logging infrastructure.
    /// The log file is locked while the guard exists, ensuring that there
    /// is no tearing of the messages.
    ///
    /// Return `None` if the log file is closed and could not be opened.
    /// Do *not* use `unwrap()` on the result; failure can be handled simply
    /// by not logging anything.
    ///
    /// # Examples
    ///
    /// ```
    /// # use util::log::LogGuard;
    /// # use std::io::Write;
    /// if let Some(mut log) = LogGuard::new() {
    ///     writeln!(log, "test");
    /// }
    /// ```
    pub fn new() -> Option<Self> {
        let f = unsafe { bindings::qemu_log_trylock() }.cast();
        NonNull::new(f).map(Self)
    }

    /// Writes a formatted string into the log, returning any error encountered.
    ///
    /// This method is primarily used by the
    /// [`log_mask_ln!()`](crate::log_mask_ln) macro, and it is rare for it
    /// to be called explicitly.  It is public because it is the only way to
    /// examine the error, which `log_mask_ln!()` ignores
    ///
    /// Unlike `log_mask_ln!()`, it does *not* append a newline at the end.
    pub fn log_fmt(args: std::fmt::Arguments) -> io::Result<()> {
        if let Some(mut log) = Self::new() {
            log.write_fmt(args)?;
        }
        Ok(())
    }
}

impl Write for LogGuard {
    fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
        let ret = unsafe {
            bindings::rust_fwrite(bytes.as_ptr().cast(), 1, bytes.len(), self.0.as_ptr())
        };
        errno::into_io_result(ret)
    }

    fn flush(&mut self) -> io::Result<()> {
        // Do nothing, dropping the guard takes care of flushing
        Ok(())
    }
}

impl Drop for LogGuard {
    fn drop(&mut self) {
        unsafe {
            bindings::qemu_log_unlock(self.0.as_ptr());
        }
    }
}

/// A macro to log messages conditionally based on a provided mask.
///
/// The `log_mask_ln` macro checks whether the given mask matches the current
/// log level and, if so, formats and logs the message. It is the Rust
/// counterpart of the `qemu_log_mask()` macro in the C implementation.
///
/// Errors from writing to the log are ignored.
///
/// # Parameters
///
/// - `$mask`: A log level mask. This should be a variant of the `Log` enum.
/// - `$fmt`: A format string following the syntax and rules of the `format!`
///   macro. It specifies the structure of the log message.
/// - `$args`: Optional arguments to be interpolated into the format string.
///
/// # Example
///
/// ```
/// use util::{log::Log, log_mask_ln};
///
/// let error_address = 0xbad;
/// log_mask_ln!(Log::GuestError, "Address 0x{error_address:x} out of range");
/// ```
///
/// It is also possible to use printf-style formatting, as well as having a
/// trailing `,`:
///
/// ```
/// use util::{log::Log, log_mask_ln};
///
/// let error_address = 0xbad;
/// log_mask_ln!(
///     Log::GuestError,
///     "Address 0x{:x} out of range",
///     error_address,
/// );
/// ```
#[macro_export]
macro_rules! log_mask_ln {
    ($mask:expr, $fmt:tt $($args:tt)*) => {{
        // Type assertion to enforce type `Log` for $mask
        let _: $crate::log::Log = $mask;

        if unsafe {
            ($crate::bindings::qemu_loglevel & ($mask as std::os::raw::c_uint)) != 0
        } {
            _ = $crate::log::LogGuard::log_fmt(
                format_args!("{}\n", format_args!($fmt $($args)*)));
        }
    }};
}
