// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

#![cfg_attr(not(MESON), doc = include_str!("../README.md"))]
#![deny(clippy::missing_const_for_fn)]

#[rustfmt::skip]
pub mod bindings;

// preserve one-item-per-"use" syntax, it is clearer
// for prelude-like modules
#[rustfmt::skip]
pub mod prelude;

pub mod chardev;
pub mod irq;
pub mod memory;
pub mod qdev;
pub mod sysbus;

// Allow proc-macros to refer to `::qemu_api` inside the `qemu_api` crate (this
// crate).
extern crate self as qemu_api;
