// SPDX-License-Identifier: GPL-2.0-or-later

pub use hwcore_sys as bindings;
pub use qemu_macros::Device;
pub use qom;

mod irq;
pub use irq::*;

// preserve one-item-per-"use" syntax, it is clearer
// for prelude-like modules
#[rustfmt::skip]
pub mod prelude;

mod qdev;
pub use qdev::*;
