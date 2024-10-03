// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::sync::OnceLock;

use crate::bindings::Property;

#[macro_export]
macro_rules! device_class_init {
    ($func:ident, props => $props:ident, realize_fn => $realize_fn:expr, legacy_reset_fn => $legacy_reset_fn:expr, vmsd => $vmsd:ident$(,)*) => {
        #[no_mangle]
        pub unsafe extern "C" fn $func(
            klass: *mut $crate::bindings::ObjectClass,
            _: *mut ::core::ffi::c_void,
        ) {
            let mut dc =
                ::core::ptr::NonNull::new(klass.cast::<$crate::bindings::DeviceClass>()).unwrap();
            dc.as_mut().realize = $realize_fn;
            dc.as_mut().vmsd = &$vmsd;
            $crate::bindings::device_class_set_legacy_reset(dc.as_mut(), $legacy_reset_fn);
            $crate::bindings::device_class_set_props(dc.as_mut(), $props.as_mut_ptr());
        }
    };
}

#[macro_export]
macro_rules! define_property {
    ($name:expr, $state:ty, $field:expr, $prop:expr, $type:expr, default = $defval:expr$(,)*) => {
        $crate::bindings::Property {
            name: {
                #[used]
                static _TEMP: &::core::ffi::CStr = $name;
                _TEMP.as_ptr()
            },
            info: $prop,
            offset: ::core::mem::offset_of!($state, $field)
                .try_into()
                .expect("Could not fit offset value to type"),
            bitnr: 0,
            bitmask: 0,
            set_default: true,
            defval: $crate::bindings::Property__bindgen_ty_1 { u: $defval.into() },
            arrayoffset: 0,
            arrayinfo: ::core::ptr::null(),
            arrayfieldsize: 0,
            link_type: ::core::ptr::null(),
        }
    };
    ($name:expr, $state:ty, $field:expr, $prop:expr, $type:expr$(,)*) => {
        $crate::bindings::Property {
            name: {
                #[used]
                static _TEMP: &::core::ffi::CStr = $name;
                _TEMP.as_ptr()
            },
            info: $prop,
            offset: ::core::mem::offset_of!($state, $field)
                .try_into()
                .expect("Could not fit offset value to type"),
            bitnr: 0,
            bitmask: 0,
            set_default: false,
            defval: $crate::bindings::Property__bindgen_ty_1 { i: 0 },
            arrayoffset: 0,
            arrayinfo: ::core::ptr::null(),
            arrayfieldsize: 0,
            link_type: ::core::ptr::null(),
        }
    };
}

#[repr(C)]
pub struct Properties<const N: usize>(pub OnceLock<[Property; N]>, pub fn() -> [Property; N]);

impl<const N: usize> Properties<N> {
    pub fn as_mut_ptr(&mut self) -> *mut Property {
        _ = self.0.get_or_init(self.1);
        self.0.get_mut().unwrap().as_mut_ptr()
    }
}

#[macro_export]
macro_rules! declare_properties {
    ($ident:ident, $($prop:expr),*$(,)*) => {

        const fn _calc_prop_len() -> usize {
            let mut len = 1;
            $({
                _ = stringify!($prop);
                len += 1;
            })*
            len
        }
        const PROP_LEN: usize = _calc_prop_len();

        fn _make_properties() -> [$crate::bindings::Property; PROP_LEN] {
            [
                $($prop),*,
                    unsafe { ::core::mem::MaybeUninit::<$crate::bindings::Property>::zeroed().assume_init() },
            ]
        }

        #[no_mangle]
        pub static mut $ident: $crate::device_class::Properties<PROP_LEN> = $crate::device_class::Properties(::std::sync::OnceLock::new(), _make_properties);
    };
}

#[macro_export]
macro_rules! vm_state_description {
    ($(#[$outer:meta])*
     $name:ident,
     $(name: $vname:expr,)*
     $(unmigratable: $um_val:expr,)*
    ) => {
        #[used]
        $(#[$outer])*
        pub static $name: $crate::bindings::VMStateDescription = $crate::bindings::VMStateDescription {
            $(name: {
                #[used]
                static VMSTATE_NAME: &::core::ffi::CStr = $vname;
                $vname.as_ptr()
            },)*
            unmigratable: true,
            ..unsafe { ::core::mem::MaybeUninit::<$crate::bindings::VMStateDescription>::zeroed().assume_init() }
        };
    }
}
