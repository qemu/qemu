//! Essential types and traits intended for blanket imports.

pub use crate::bitops::IntegerExt;
pub use crate::uninit::MaybeUninitField;

// Re-export commonly used macros
pub use crate::static_assert;
pub use crate::uninit_field_mut;
pub use qemu_macros::TryInto;
