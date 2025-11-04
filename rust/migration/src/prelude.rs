//! Essential types and traits intended for blanket imports.

// Core migration traits and types
pub use crate::vmstate::VMState;
pub use crate::vmstate::VMStateDescription;
pub use crate::vmstate::VMStateDescriptionBuilder;

// Migratable wrappers
pub use crate::migratable::Migratable;
pub use crate::ToMigrationState;

// Commonly used macros
pub use crate::impl_vmstate_forward;
pub use crate::impl_vmstate_struct;
pub use crate::vmstate_fields;
pub use crate::vmstate_of;
pub use crate::vmstate_subsections;
pub use crate::vmstate_unused;
pub use crate::vmstate_validate;
