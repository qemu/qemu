// Copyright 2024 Red Hat, Inc.
// Author(s): Paolo Bonzini <pbonzini@redhat.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#![doc(hidden)]
//! This module provides a macro to define a constant of type
//! [`CStr`](std::ffi::CStr), for compatibility with versions of
//! Rust that lack `c""` literals.
//!
//! Documentation is hidden because it only exposes macros, which
//! are exported directly from `qemu_api`.

#[macro_export]
/// Given a string constant _without_ embedded or trailing NULs, return
/// a `CStr`.
///
/// Needed for compatibility with Rust <1.77.
macro_rules! c_str {
    ($str:expr) => {{
        const STRING: &str = concat!($str, "\0");
        const BYTES: &[u8] = STRING.as_bytes();

        // "for" is not allowed in const context... oh well,
        // everybody loves some lisp.  This could be turned into
        // a procedural macro if this is a problem; alternatively
        // Rust 1.72 makes CStr::from_bytes_with_nul a const function.
        const fn f(b: &[u8], i: usize) {
            if i == b.len() - 1 {
            } else if b[i] == 0 {
                panic!("c_str argument contains NUL")
            } else {
                f(b, i + 1)
            }
        }
        f(BYTES, 0);

        // SAFETY: absence of NULs apart from the final byte was checked above
        unsafe { std::ffi::CStr::from_bytes_with_nul_unchecked(BYTES) }
    }};
}

#[cfg(test)]
mod tests {
    use std::ffi::CStr;

    use crate::c_str;

    #[test]
    fn test_cstr_macro() {
        let good = c_str!("🦀");
        let good_bytes = b"\xf0\x9f\xa6\x80\0";
        assert_eq!(good.to_bytes_with_nul(), good_bytes);
    }

    #[test]
    fn test_cstr_macro_const() {
        const GOOD: &CStr = c_str!("🦀");
        const GOOD_BYTES: &[u8] = b"\xf0\x9f\xa6\x80\0";
        assert_eq!(GOOD.to_bytes_with_nul(), GOOD_BYTES);
    }
}
