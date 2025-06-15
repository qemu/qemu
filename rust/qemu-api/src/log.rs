// Copyright 2025 Bernhard Beschow <shentey@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Bindings for QEMU's logging infrastructure

#[repr(u32)]
/// Represents specific error categories within QEMU's logging system.
///
/// The `Log` enum provides a Rust abstraction for logging errors, corresponding
/// to a subset of the error categories defined in the C implementation.
pub enum Log {
    /// Log invalid access caused by the guest.
    /// Corresponds to `LOG_GUEST_ERROR` in the C implementation.
    GuestError = crate::bindings::LOG_GUEST_ERROR,

    /// Log guest access of unimplemented functionality.
    /// Corresponds to `LOG_UNIMP` in the C implementation.
    Unimp = crate::bindings::LOG_UNIMP,
}

/// A macro to log messages conditionally based on a provided mask.
///
/// The `log_mask_ln` macro checks whether the given mask matches the current
/// log level and, if so, formats and logs the message. It is the Rust
/// counterpart of the `qemu_log_mask()` macro in the C implementation.
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
/// use qemu_api::{log::Log, log_mask_ln};
///
/// let error_address = 0xbad;
/// log_mask_ln!(Log::GuestError, "Address 0x{error_address:x} out of range");
/// ```
///
/// It is also possible to use printf-style formatting, as well as having a
/// trailing `,`:
///
/// ```
/// use qemu_api::{log::Log, log_mask_ln};
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
        let _: Log = $mask;

        if unsafe {
            (::qemu_api::bindings::qemu_loglevel & ($mask as std::os::raw::c_int)) != 0
        } {
            let formatted_string = format!("{}\n", format_args!($fmt $($args)*));
            let c_string = std::ffi::CString::new(formatted_string).unwrap();

            unsafe {
                ::qemu_api::bindings::qemu_log(c_string.as_ptr());
            }
        }
    }};
}
