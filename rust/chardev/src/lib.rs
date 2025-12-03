// SPDX-License-Identifier: GPL-2.0-or-later

pub use chardev_sys as bindings;

mod chardev;
pub use chardev::*;

// preserve one-item-per-"use" syntax, it is clearer
// for prelude-like modules
#[rustfmt::skip]
pub mod prelude;
