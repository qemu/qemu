// SPDX-License-Identifier: GPL-2.0-or-later

pub mod bindings;

// preserve one-item-per-"use" syntax, it is clearer
// for prelude-like modules
#[rustfmt::skip]
pub mod prelude;

mod qom;
pub use qom::*;
