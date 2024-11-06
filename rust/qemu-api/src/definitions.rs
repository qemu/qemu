// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Definitions required by QEMU when registering a device.

use std::{ffi::CStr, os::raw::c_void};

use crate::bindings::{Object, ObjectClass, TypeInfo};

/// Trait a type must implement to be registered with QEMU.
pub trait ObjectImpl {
    type Class;
    const TYPE_INFO: TypeInfo;
    const TYPE_NAME: &'static CStr;
    const PARENT_TYPE_NAME: Option<&'static CStr>;
    const ABSTRACT: bool;
    const INSTANCE_INIT: Option<unsafe extern "C" fn(obj: *mut Object)>;
    const INSTANCE_POST_INIT: Option<unsafe extern "C" fn(obj: *mut Object)>;
    const INSTANCE_FINALIZE: Option<unsafe extern "C" fn(obj: *mut Object)>;
}

pub trait Class {
    const CLASS_INIT: Option<unsafe extern "C" fn(klass: *mut ObjectClass, data: *mut c_void)>;
    const CLASS_BASE_INIT: Option<
        unsafe extern "C" fn(klass: *mut ObjectClass, data: *mut c_void),
    >;
}

#[macro_export]
macro_rules! module_init {
    ($type:ident => $body:block) => {
        const _: () = {
            #[used]
            #[cfg_attr(
                not(any(target_vendor = "apple", target_os = "windows")),
                link_section = ".init_array"
            )]
            #[cfg_attr(target_vendor = "apple", link_section = "__DATA,__mod_init_func")]
            #[cfg_attr(target_os = "windows", link_section = ".CRT$XCU")]
            pub static LOAD_MODULE: extern "C" fn() = {
                extern "C" fn init_fn() {
                    $body
                }

                extern "C" fn ctor_fn() {
                    unsafe {
                        $crate::bindings::register_module_init(
                            Some(init_fn),
                            $crate::bindings::module_init_type::$type,
                        );
                    }
                }

                ctor_fn
            };
        };
    };

    // shortcut because it's quite common that $body needs unsafe {}
    ($type:ident => unsafe $body:block) => {
        $crate::module_init! {
            $type => { unsafe { $body } }
        }
    };
}

#[macro_export]
macro_rules! type_info {
    ($t:ty) => {
        $crate::bindings::TypeInfo {
            name: <$t as $crate::definitions::ObjectImpl>::TYPE_NAME.as_ptr(),
            parent: if let Some(pname) = <$t as $crate::definitions::ObjectImpl>::PARENT_TYPE_NAME {
                pname.as_ptr()
            } else {
                ::core::ptr::null_mut()
            },
            instance_size: ::core::mem::size_of::<$t>(),
            instance_align: ::core::mem::align_of::<$t>(),
            instance_init: <$t as $crate::definitions::ObjectImpl>::INSTANCE_INIT,
            instance_post_init: <$t as $crate::definitions::ObjectImpl>::INSTANCE_POST_INIT,
            instance_finalize: <$t as $crate::definitions::ObjectImpl>::INSTANCE_FINALIZE,
            abstract_: <$t as $crate::definitions::ObjectImpl>::ABSTRACT,
            class_size:  ::core::mem::size_of::<<$t as $crate::definitions::ObjectImpl>::Class>(),
            class_init: <<$t as $crate::definitions::ObjectImpl>::Class as $crate::definitions::Class>::CLASS_INIT,
            class_base_init: <<$t as $crate::definitions::ObjectImpl>::Class as $crate::definitions::Class>::CLASS_BASE_INIT,
            class_data: ::core::ptr::null_mut(),
            interfaces: ::core::ptr::null_mut(),
        };
    }
}
