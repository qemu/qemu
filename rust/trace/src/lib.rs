// SPDX-License-Identifier: GPL-2.0-or-later

//! This crate provides macros that aid in using QEMU's tracepoint
//! functionality.

#[doc(hidden)]
/// Re-exported item to avoid adding libc as a dependency everywhere.
pub use libc::{syslog, LOG_INFO};

#[macro_export]
/// Define the trace-points from the named directory (which should have slashes
/// replaced by underscore characters) as functions in a module called `trace`.
///
/// ```ignore
/// ::trace::include_trace!("hw_char");
/// // ...
/// trace::trace_pl011_read_fifo_rx_full();
/// ```
macro_rules! include_trace {
    ($name:literal) => {
        #[allow(
            clippy::ptr_as_ptr,
            clippy::cast_lossless,
            clippy::used_underscore_binding
        )]
        mod trace {
            #[cfg(not(MESON))]
            include!(concat!(
                env!("MESON_BUILD_ROOT"),
                "/trace/trace-",
                $name,
                ".rs"
            ));

            #[cfg(MESON)]
            include!(concat!("@MESON_BUILD_ROOT@/trace/trace-", $name, ".rs"));
        }
    };
}
