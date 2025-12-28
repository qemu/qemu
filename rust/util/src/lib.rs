// SPDX-License-Identifier: GPL-2.0-or-later

pub mod bindings;
pub mod error;
pub mod log;
pub mod module;

// preserve one-item-per-"use" syntax, it is clearer
// for prelude-like modules
#[rustfmt::skip]
pub mod prelude;
pub mod timer;

pub use error::{Error, Result, ResultExt};
