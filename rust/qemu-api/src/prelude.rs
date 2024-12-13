// Copyright 2024 Red Hat, Inc.
// Author(s): Paolo Bonzini <pbonzini@redhat.com>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Commonly used traits and types for QEMU.

pub use crate::bitops::IntegerExt;

pub use crate::cell::BqlCell;
pub use crate::cell::BqlRefCell;

pub use crate::errno;

pub use crate::qdev::DeviceMethods;

pub use crate::qom::InterfaceType;
pub use crate::qom::IsA;
pub use crate::qom::Object;
pub use crate::qom::ObjectCast;
pub use crate::qom::ObjectDeref;
pub use crate::qom::ObjectClassMethods;
pub use crate::qom::ObjectMethods;
pub use crate::qom::ObjectType;

pub use crate::qom_isa;

pub use crate::sysbus::SysBusDeviceMethods;

pub use crate::vmstate::VMState;
