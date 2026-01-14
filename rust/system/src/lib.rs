// SPDX-License-Identifier: GPL-2.0-or-later

pub use system_sys as bindings;

mod memory;
pub use memory::*;

// preserve one-item-per-"use" syntax, it is clearer
// for prelude-like modules
#[rustfmt::skip]
pub mod prelude;

mod sysbus;
pub use sysbus::*;
