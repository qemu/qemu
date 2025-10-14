// Copyright 2025 Red Hat, Inc.
// Author(s): Paolo Bonzini <pbonzini@redhat.com>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::{
    fmt,
    mem::size_of,
    ptr::{self, addr_of, NonNull},
    sync::{Arc, Mutex},
};

use bql::{BqlCell, BqlRefCell};
use common::Zeroable;

use crate::{
    bindings, vmstate_fields_ref, vmstate_of, InvalidError, VMState, VMStateDescriptionBuilder,
};

/// Enables QEMU migration support even when a type is wrapped with
/// synchronization primitives (like `Mutex`) that the C migration
/// code cannot directly handle. The trait provides methods to
/// extract essential state for migration and restore it after
/// migration completes.
///
/// On top of extracting data from synchronization wrappers during save
/// and restoring it during load, it's also possible to use `ToMigrationState`
/// to convert runtime representations to migration-safe formats.
///
/// # Examples
///
/// ```
/// use bql::BqlCell;
/// use migration::{InvalidError, ToMigrationState, VMState};
/// # use migration::VMStateField;
///
/// # #[derive(Debug, PartialEq, Eq)]
/// struct DeviceState {
///     counter: BqlCell<u32>,
///     enabled: bool,
/// }
///
/// # #[derive(Debug)]
/// #[derive(Default)]
/// struct DeviceMigrationState {
///     counter: u32,
///     enabled: bool,
/// }
///
/// # unsafe impl VMState for DeviceMigrationState {
/// #     const BASE: VMStateField = ::common::Zeroable::ZERO;
/// # }
/// impl ToMigrationState for DeviceState {
///     type Migrated = DeviceMigrationState;
///
///     fn snapshot_migration_state(
///         &self,
///         target: &mut Self::Migrated,
///     ) -> Result<(), InvalidError> {
///         target.counter = self.counter.get();
///         target.enabled = self.enabled;
///         Ok(())
///     }
///
///     fn restore_migrated_state_mut(
///         &mut self,
///         source: Self::Migrated,
///         _version_id: u8,
///     ) -> Result<(), InvalidError> {
///         self.counter.set(source.counter);
///         self.enabled = source.enabled;
///         Ok(())
///     }
/// }
/// # bql::start_test();
/// # let dev = DeviceState { counter: 10.into(), enabled: true };
/// # let mig = dev.to_migration_state().unwrap();
/// # assert!(matches!(*mig, DeviceMigrationState { counter: 10, enabled: true }));
/// # let mut dev2 = DeviceState { counter: 42.into(), enabled: false };
/// # dev2.restore_migrated_state_mut(*mig, 1).unwrap();
/// # assert_eq!(dev2, dev);
/// ```
///
/// More commonly, the trait is derived through the
/// [`derive(ToMigrationState)`](qemu_macros::ToMigrationState) procedural
/// macro.
pub trait ToMigrationState {
    /// The type used to represent the migrated state.
    type Migrated: Default + VMState;

    /// Capture the current state into a migration-safe format, failing
    /// if the state cannot be migrated.
    fn snapshot_migration_state(&self, target: &mut Self::Migrated) -> Result<(), InvalidError>;

    /// Restores state from a migrated representation, failing if the
    /// state cannot be restored.
    fn restore_migrated_state_mut(
        &mut self,
        source: Self::Migrated,
        version_id: u8,
    ) -> Result<(), InvalidError>;

    /// Convenience method to combine allocation and state capture
    /// into a single operation.
    fn to_migration_state(&self) -> Result<Box<Self::Migrated>, InvalidError> {
        let mut migrated = Box::<Self::Migrated>::default();
        self.snapshot_migration_state(&mut migrated)?;
        Ok(migrated)
    }
}

// Implementations for primitive types.  Do not use a blanket implementation
// for all Copy types, because [T; N] is Copy if T is Copy; that would conflict
// with the below implementation for arrays.
macro_rules! impl_for_primitive {
    ($($t:ty),*) => {
        $(
            impl ToMigrationState for $t {
                type Migrated = Self;

                fn snapshot_migration_state(
                    &self,
                    target: &mut Self::Migrated,
                ) -> Result<(), InvalidError> {
                    *target = *self;
                    Ok(())
                }

                fn restore_migrated_state_mut(
                    &mut self,
                    source: Self::Migrated,
                    _version_id: u8,
                ) -> Result<(), InvalidError> {
                    *self = source;
                    Ok(())
                }
            }
        )*
    };
}

impl_for_primitive!(u8, u16, u32, u64, i8, i16, i32, i64, bool);

impl<T: ToMigrationState, const N: usize> ToMigrationState for [T; N]
where
    [T::Migrated; N]: Default,
{
    type Migrated = [T::Migrated; N];

    fn snapshot_migration_state(&self, target: &mut Self::Migrated) -> Result<(), InvalidError> {
        for (item, target_item) in self.iter().zip(target.iter_mut()) {
            item.snapshot_migration_state(target_item)?;
        }
        Ok(())
    }

    fn restore_migrated_state_mut(
        &mut self,
        source: Self::Migrated,
        version_id: u8,
    ) -> Result<(), InvalidError> {
        for (item, source_item) in self.iter_mut().zip(source) {
            item.restore_migrated_state_mut(source_item, version_id)?;
        }
        Ok(())
    }
}

impl<T: ToMigrationState> ToMigrationState for Mutex<T> {
    type Migrated = T::Migrated;

    fn snapshot_migration_state(&self, target: &mut Self::Migrated) -> Result<(), InvalidError> {
        self.lock().unwrap().snapshot_migration_state(target)
    }

    fn restore_migrated_state_mut(
        &mut self,
        source: Self::Migrated,
        version_id: u8,
    ) -> Result<(), InvalidError> {
        self.get_mut()
            .unwrap()
            .restore_migrated_state_mut(source, version_id)
    }
}

impl<T: ToMigrationState> ToMigrationState for BqlRefCell<T> {
    type Migrated = T::Migrated;

    fn snapshot_migration_state(&self, target: &mut Self::Migrated) -> Result<(), InvalidError> {
        self.borrow().snapshot_migration_state(target)
    }

    fn restore_migrated_state_mut(
        &mut self,
        source: Self::Migrated,
        version_id: u8,
    ) -> Result<(), InvalidError> {
        self.get_mut()
            .restore_migrated_state_mut(source, version_id)
    }
}

/// Extension trait for types that support migration state restoration
/// through interior mutability.
///
/// This trait extends [`ToMigrationState`] for types that can restore
/// their state without requiring mutable access. While user structs
/// will generally use `ToMigrationState`, the device will have multiple
/// references and therefore the device struct has to employ an interior
/// mutability wrapper like [`Mutex`] or [`BqlRefCell`].
///
/// Anything that implements this trait can in turn be used within
/// [`Migratable<T>`], which makes no assumptions on how to achieve mutable
/// access to the runtime state.
///
/// # Examples
///
/// ```
/// use std::sync::Mutex;
///
/// use migration::ToMigrationStateShared;
///
/// let device_state = Mutex::new(42);
/// // Can restore without &mut access
/// device_state.restore_migrated_state(100, 1).unwrap();
/// assert_eq!(*device_state.lock().unwrap(), 100);
/// ```
pub trait ToMigrationStateShared: ToMigrationState {
    /// Restores state from a migrated representation to an interior-mutable
    /// object.  Similar to `restore_migrated_state_mut`, but requires a
    /// shared reference; therefore it can be used to restore a device's
    /// state even though devices have multiple references to them.
    fn restore_migrated_state(
        &self,
        source: Self::Migrated,
        version_id: u8,
    ) -> Result<(), InvalidError>;
}

impl<T: ToMigrationStateShared, const N: usize> ToMigrationStateShared for [T; N]
where
    [T::Migrated; N]: Default,
{
    fn restore_migrated_state(
        &self,
        source: Self::Migrated,
        version_id: u8,
    ) -> Result<(), InvalidError> {
        for (item, source_item) in self.iter().zip(source) {
            item.restore_migrated_state(source_item, version_id)?;
        }
        Ok(())
    }
}

// Arc requires the contained object to be interior-mutable
impl<T: ToMigrationStateShared> ToMigrationState for Arc<T> {
    type Migrated = T::Migrated;

    fn snapshot_migration_state(&self, target: &mut Self::Migrated) -> Result<(), InvalidError> {
        (**self).snapshot_migration_state(target)
    }

    fn restore_migrated_state_mut(
        &mut self,
        source: Self::Migrated,
        version_id: u8,
    ) -> Result<(), InvalidError> {
        (**self).restore_migrated_state(source, version_id)
    }
}

impl<T: ToMigrationStateShared> ToMigrationStateShared for Arc<T> {
    fn restore_migrated_state(
        &self,
        source: Self::Migrated,
        version_id: u8,
    ) -> Result<(), InvalidError> {
        (**self).restore_migrated_state(source, version_id)
    }
}

// Interior-mutable types.  Note how they only require ToMigrationState for
// the inner type!

impl<T: ToMigrationState> ToMigrationStateShared for Mutex<T> {
    fn restore_migrated_state(
        &self,
        source: Self::Migrated,
        version_id: u8,
    ) -> Result<(), InvalidError> {
        self.lock()
            .unwrap()
            .restore_migrated_state_mut(source, version_id)
    }
}

impl<T: ToMigrationState> ToMigrationStateShared for BqlRefCell<T> {
    fn restore_migrated_state(
        &self,
        source: Self::Migrated,
        version_id: u8,
    ) -> Result<(), InvalidError> {
        self.borrow_mut()
            .restore_migrated_state_mut(source, version_id)
    }
}

/// A wrapper that enables QEMU migration for types with shared state.
///
/// `Migratable<T>` provides a bridge between Rust types that use interior
/// mutability (like `Mutex<T>`) and QEMU's C-based migration infrastructure.
/// It manages the lifecycle of migration state and provides automatic
/// conversion between runtime and migration representations.
///
/// ```
/// # use std::sync::Mutex;
/// # use migration::{Migratable, ToMigrationState, VMState, VMStateField};
///
/// #[derive(ToMigrationState)]
/// pub struct DeviceRegs {
///     status: u32,
/// }
/// # unsafe impl VMState for DeviceRegsMigration {
/// #     const BASE: VMStateField = ::common::Zeroable::ZERO;
/// # }
///
/// pub struct SomeDevice {
///     // ...
///     registers: Migratable<Mutex<DeviceRegs>>,
/// }
/// ```
#[repr(C)]
pub struct Migratable<T: ToMigrationStateShared> {
    /// Pointer to migration state, valid only during migration operations.
    /// C vmstate does not support NULL pointers, so no `Option<Box<>>`.
    migration_state: BqlCell<*mut T::Migrated>,

    /// The runtime state that can be accessed during normal operation
    runtime_state: T,
}

impl<T: ToMigrationStateShared> std::ops::Deref for Migratable<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &self.runtime_state
    }
}

impl<T: ToMigrationStateShared> std::ops::DerefMut for Migratable<T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.runtime_state
    }
}

impl<T: ToMigrationStateShared> Migratable<T> {
    /// Creates a new `Migratable` wrapper around the given runtime state.
    ///
    /// # Returns
    /// A new `Migratable` instance ready for use and migration
    pub fn new(runtime_state: T) -> Self {
        Self {
            migration_state: BqlCell::new(ptr::null_mut()),
            runtime_state,
        }
    }

    fn pre_save(&self) -> Result<(), InvalidError> {
        let state = self.runtime_state.to_migration_state()?;
        self.migration_state.set(Box::into_raw(state));
        Ok(())
    }

    fn post_save(&self) -> Result<(), InvalidError> {
        let state = unsafe { Box::from_raw(self.migration_state.replace(ptr::null_mut())) };
        drop(state);
        Ok(())
    }

    fn pre_load(&self) -> Result<(), InvalidError> {
        self.migration_state
            .set(Box::into_raw(Box::<T::Migrated>::default()));
        Ok(())
    }

    fn post_load(&self, version_id: u8) -> Result<(), InvalidError> {
        let state = unsafe { Box::from_raw(self.migration_state.replace(ptr::null_mut())) };
        self.runtime_state
            .restore_migrated_state(*state, version_id)
    }
}

impl<T: ToMigrationStateShared + fmt::Debug> fmt::Debug for Migratable<T>
where
    T::Migrated: fmt::Debug,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut struct_f = f.debug_struct("Migratable");
        struct_f.field("runtime_state", &self.runtime_state);

        let state = NonNull::new(self.migration_state.get()).map(|x| unsafe { x.as_ref() });
        struct_f.field("migration_state", &state);
        struct_f.finish()
    }
}

impl<T: ToMigrationStateShared + Default> Default for Migratable<T> {
    fn default() -> Self {
        Self::new(T::default())
    }
}

impl<T: 'static + ToMigrationStateShared> Migratable<T> {
    const FIELD: bindings::VMStateField = vmstate_of!(Self, migration_state);

    const FIELDS: &[bindings::VMStateField] = vmstate_fields_ref! {
        Migratable::<T>::FIELD
    };

    const VMSD: &'static bindings::VMStateDescription = VMStateDescriptionBuilder::<Self>::new()
        .version_id(1)
        .minimum_version_id(1)
        .pre_save(&Self::pre_save)
        .pre_load(&Self::pre_load)
        .post_save(&Self::post_save)
        .post_load(&Self::post_load)
        .fields(Self::FIELDS)
        .build()
        .as_ref();
}

unsafe impl<T: 'static + ToMigrationStateShared> VMState for Migratable<T> {
    const BASE: bindings::VMStateField = {
        bindings::VMStateField {
            vmsd: addr_of!(*Self::VMSD),
            size: size_of::<Self>(),
            flags: bindings::VMStateFlags::VMS_STRUCT,
            ..Zeroable::ZERO
        }
    };
}
