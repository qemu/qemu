// Copyright 2024 Red Hat, Inc.
// Author(s): Paolo Bonzini <pbonzini@redhat.com>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Bindings for character devices

use std::ffi::CStr;

use crate::{bindings, prelude::*};

pub type Chardev = bindings::Chardev;
pub type ChardevClass = bindings::ChardevClass;

unsafe impl ObjectType for Chardev {
    type Class = ChardevClass;
    const TYPE_NAME: &'static CStr =
        unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_CHARDEV) };
}
qom_isa!(Chardev: Object);
