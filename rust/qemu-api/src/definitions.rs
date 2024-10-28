// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Definitions required by QEMU when registering a device.

use std::{ffi::CStr, os::raw::c_void};

use crate::bindings::{Object, ObjectClass, TypeInfo};

/// Trait a type must implement to be registered with QEMU.
pub trait ObjectImpl: Sized {
    type Class: ClassInitImpl;
    const TYPE_NAME: &'static CStr;
    const PARENT_TYPE_NAME: Option<&'static CStr>;
    const ABSTRACT: bool = false;
    const INSTANCE_INIT: Option<unsafe extern "C" fn(obj: *mut Object)> = None;
    const INSTANCE_POST_INIT: Option<unsafe extern "C" fn(obj: *mut Object)> = None;
    const INSTANCE_FINALIZE: Option<unsafe extern "C" fn(obj: *mut Object)> = None;

    const TYPE_INFO: TypeInfo = TypeInfo {
        name: Self::TYPE_NAME.as_ptr(),
        parent: if let Some(pname) = Self::PARENT_TYPE_NAME {
            pname.as_ptr()
        } else {
            core::ptr::null_mut()
        },
        instance_size: core::mem::size_of::<Self>(),
        instance_align: core::mem::align_of::<Self>(),
        instance_init: Self::INSTANCE_INIT,
        instance_post_init: Self::INSTANCE_POST_INIT,
        instance_finalize: Self::INSTANCE_FINALIZE,
        abstract_: Self::ABSTRACT,
        class_size: core::mem::size_of::<Self::Class>(),
        class_init: <Self::Class as ClassInitImpl>::CLASS_INIT,
        class_base_init: <Self::Class as ClassInitImpl>::CLASS_BASE_INIT,
        class_data: core::ptr::null_mut(),
        interfaces: core::ptr::null_mut(),
    };
}

/// Trait used to fill in a class struct.
///
/// Each QOM class that has virtual methods describes them in a
/// _class struct_.  Class structs include a parent field corresponding
/// to the vtable of the parent class, all the way up to [`ObjectClass`].
/// Each QOM type has one such class struct.
///
/// The Rust implementation of methods will usually come from a trait
/// like [`ObjectImpl`].
pub trait ClassInitImpl {
    /// Function that is called after all parent class initialization
    /// has occurred.  On entry, the virtual method pointers are set to
    /// the default values coming from the parent classes; the function
    /// can change them to override virtual methods of a parent class.
    const CLASS_INIT: Option<unsafe extern "C" fn(klass: *mut ObjectClass, data: *mut c_void)>;

    /// Called on descendent classes after all parent class initialization
    /// has occurred, but before the class itself is initialized.  This
    /// is only useful if a class is not a leaf, and can be used to undo
    /// the effects of copying the contents of the parent's class struct
    /// to the descendants.
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
