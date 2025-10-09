// SPDX-License-Identifier: GPL-2.0-or-later

pub mod bindings;

pub use qemu_macros::ToMigrationState;

pub mod migratable;
pub use migratable::*;

pub mod vmstate;
pub use vmstate::*;
