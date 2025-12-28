// SPDX-License-Identifier: GPL-2.0-or-later

pub mod bindings;

mod memory;
pub use memory::*;

// preserve one-item-per-"use" syntax, it is clearer
// for prelude-like modules
#[rustfmt::skip]
pub mod prelude;
