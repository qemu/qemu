// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::ffi::CStr;

use crate::bindings;

#[macro_export]
macro_rules! device_class_init {
    ($func:ident, props => $props:ident, realize_fn => $realize_fn:expr, legacy_reset_fn => $legacy_reset_fn:expr, vmsd => $vmsd:ident$(,)*) => {
        pub unsafe extern "C" fn $func(
            klass: *mut $crate::bindings::ObjectClass,
            _: *mut ::std::os::raw::c_void,
        ) {
            let mut dc =
                ::core::ptr::NonNull::new(klass.cast::<$crate::bindings::DeviceClass>()).unwrap();
            unsafe {
                dc.as_mut().realize = $realize_fn;
                dc.as_mut().vmsd = &$vmsd;
                $crate::bindings::device_class_set_legacy_reset(dc.as_mut(), $legacy_reset_fn);
                $crate::bindings::device_class_set_props(dc.as_mut(), $props.as_ptr());
            }
        }
    };
}

#[macro_export]
macro_rules! define_property {
    ($name:expr, $state:ty, $field:ident, $prop:expr, $type:expr, default = $defval:expr$(,)*) => {
        $crate::bindings::Property {
            // use associated function syntax for type checking
            name: ::std::ffi::CStr::as_ptr($name),
            info: $prop,
            offset: $crate::offset_of!($state, $field) as isize,
            set_default: true,
            defval: $crate::bindings::Property__bindgen_ty_1 { u: $defval as u64 },
            ..$crate::zeroable::Zeroable::ZERO
        }
    };
    ($name:expr, $state:ty, $field:ident, $prop:expr, $type:expr$(,)*) => {
        $crate::bindings::Property {
            // use associated function syntax for type checking
            name: ::std::ffi::CStr::as_ptr($name),
            info: $prop,
            offset: $crate::offset_of!($state, $field) as isize,
            set_default: false,
            ..$crate::zeroable::Zeroable::ZERO
        }
    };
}

#[macro_export]
macro_rules! declare_properties {
    ($ident:ident, $($prop:expr),*$(,)*) => {
        pub static $ident: [$crate::bindings::Property; {
            let mut len = 1;
            $({
                _ = stringify!($prop);
                len += 1;
            })*
            len
        }] = [
            $($prop),*,
            $crate::zeroable::Zeroable::ZERO,
        ];
    };
}

// workaround until we can use --generate-cstr in bindgen.
pub const TYPE_DEVICE: &CStr =
    unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_DEVICE) };
pub const TYPE_SYS_BUS_DEVICE: &CStr =
    unsafe { CStr::from_bytes_with_nul_unchecked(bindings::TYPE_SYS_BUS_DEVICE) };
