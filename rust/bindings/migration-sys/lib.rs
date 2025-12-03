// SPDX-License-Identifier: GPL-2.0-or-later
#![allow(
    dead_code,
    improper_ctypes_definitions,
    improper_ctypes,
    non_camel_case_types,
    non_snake_case,
    non_upper_case_globals,
    unnecessary_transmutes,
    unsafe_op_in_unsafe_fn,
    clippy::pedantic,
    clippy::restriction,
    clippy::style,
    clippy::missing_const_for_fn,
    clippy::ptr_offset_with_cast,
    clippy::useless_transmute,
    clippy::missing_safety_doc,
    clippy::too_many_arguments
)]

use common::Zeroable;
use util_sys::{Error, JSONWriter, QEMUFile};

#[cfg(MESON)]
include!("bindings.inc.rs");

#[cfg(not(MESON))]
include!(concat!(env!("OUT_DIR"), "/bindings.inc.rs"));

unsafe impl Send for VMStateDescription {}
unsafe impl Sync for VMStateDescription {}

unsafe impl Send for VMStateField {}
unsafe impl Sync for VMStateField {}

unsafe impl Send for VMStateInfo {}
unsafe impl Sync for VMStateInfo {}

// bindgen does not derive Default here
#[allow(clippy::derivable_impls)]
impl Default for VMStateFlags {
    fn default() -> Self {
        Self(0)
    }
}

unsafe impl Zeroable for VMStateFlags {}
unsafe impl Zeroable for VMStateField {}
unsafe impl Zeroable for VMStateDescription {}

// The following higher-level helpers could be in "migration"
// crate when Rust has const trait impl.

pub trait VMStateFlagsExt {
    const VMS_VARRAY_FLAGS: VMStateFlags;
}

impl VMStateFlagsExt for VMStateFlags {
    const VMS_VARRAY_FLAGS: VMStateFlags = VMStateFlags(
        VMStateFlags::VMS_VARRAY_INT32.0
            | VMStateFlags::VMS_VARRAY_UINT8.0
            | VMStateFlags::VMS_VARRAY_UINT16.0
            | VMStateFlags::VMS_VARRAY_UINT32.0,
    );
}

// Add a couple builder-style methods to VMStateField, allowing
// easy derivation of VMStateField constants from other types.
impl VMStateField {
    #[must_use]
    pub const fn with_version_id(mut self, version_id: i32) -> Self {
        assert!(version_id >= 0);
        self.version_id = version_id;
        self
    }

    #[must_use]
    pub const fn with_array_flag(mut self, num: usize) -> Self {
        assert!(num <= 0x7FFF_FFFFusize);
        assert!((self.flags.0 & VMStateFlags::VMS_ARRAY.0) == 0);
        assert!((self.flags.0 & VMStateFlags::VMS_VARRAY_FLAGS.0) == 0);
        if (self.flags.0 & VMStateFlags::VMS_POINTER.0) != 0 {
            self.flags = VMStateFlags(self.flags.0 & !VMStateFlags::VMS_POINTER.0);
            self.flags = VMStateFlags(self.flags.0 | VMStateFlags::VMS_ARRAY_OF_POINTER.0);
            // VMS_ARRAY_OF_POINTER flag stores the size of pointer.
            // FIXME: *const, *mut, NonNull and Box<> have the same size as usize.
            //        Resize if more smart pointers are supported.
            self.size = std::mem::size_of::<usize>();
        }
        self.flags = VMStateFlags(self.flags.0 & !VMStateFlags::VMS_SINGLE.0);
        self.flags = VMStateFlags(self.flags.0 | VMStateFlags::VMS_ARRAY.0);
        self.num = num as i32;
        self
    }

    #[must_use]
    pub const fn with_pointer_flag(mut self) -> Self {
        assert!((self.flags.0 & VMStateFlags::VMS_POINTER.0) == 0);
        self.flags = VMStateFlags(self.flags.0 | VMStateFlags::VMS_POINTER.0);
        self
    }

    #[must_use]
    pub const fn with_varray_flag_unchecked(mut self, flag: VMStateFlags) -> Self {
        self.flags = VMStateFlags(self.flags.0 & !VMStateFlags::VMS_ARRAY.0);
        self.flags = VMStateFlags(self.flags.0 | flag.0);
        self.num = 0; // varray uses num_offset instead of num.
        self
    }

    #[must_use]
    #[allow(unused_mut)]
    pub const fn with_varray_flag(mut self, flag: VMStateFlags) -> Self {
        assert!((self.flags.0 & VMStateFlags::VMS_ARRAY.0) != 0);
        self.with_varray_flag_unchecked(flag)
    }

    #[must_use]
    pub const fn with_varray_multiply(mut self, num: u32) -> Self {
        assert!(num <= 0x7FFF_FFFFu32);
        self.flags = VMStateFlags(self.flags.0 | VMStateFlags::VMS_MULTIPLY_ELEMENTS.0);
        self.num = num as i32;
        self
    }
}
