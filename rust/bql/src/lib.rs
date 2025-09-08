// SPDX-License-Identifier: GPL-2.0-or-later

mod bindings;
use bindings::{bql_block_unlock, bql_locked, rust_bql_mock_lock};

mod cell;
pub use cell::*;

/// An internal function that is used by doctests.
pub fn start_test() {
    // SAFETY: integration tests are run with --test-threads=1, while
    // unit tests and doctests are not multithreaded and do not have
    // any BQL-protected data.  Just set bql_locked to true.
    unsafe {
        rust_bql_mock_lock();
    }
}

pub fn is_locked() -> bool {
    // SAFETY: the function does nothing but return a thread-local bool
    unsafe { bql_locked() }
}

pub fn block_unlock(increase: bool) {
    // SAFETY: this only adjusts a counter
    unsafe {
        bql_block_unlock(increase);
    }
}
