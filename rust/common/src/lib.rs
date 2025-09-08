// SPDX-License-Identifier: GPL-2.0-or-later

pub use qemu_macros::{TryInto, Wrapper};

pub mod assertions;

pub mod bitops;

pub mod callbacks;
pub use callbacks::FnCall;

pub mod errno;
pub use errno::Errno;

pub mod opaque;
pub use opaque::{Opaque, Wrapper};

pub mod uninit;
pub use uninit::MaybeUninitField;

pub mod zeroable;
pub use zeroable::Zeroable;
