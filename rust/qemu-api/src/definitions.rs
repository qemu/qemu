// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Definitions required by QEMU when registering a device.

use std::{ffi::CStr, os::raw::c_void};

use crate::bindings::{Object, ObjectClass, TypeInfo};

unsafe extern "C" fn rust_instance_init<T: ObjectImpl>(obj: *mut Object) {
    // SAFETY: obj is an instance of T, since rust_instance_init<T>
    // is called from QOM core as the instance_init function
    // for class T
    unsafe { T::INSTANCE_INIT.unwrap()(&mut *obj.cast::<T>()) }
}

unsafe extern "C" fn rust_instance_post_init<T: ObjectImpl>(obj: *mut Object) {
    // SAFETY: obj is an instance of T, since rust_instance_post_init<T>
    // is called from QOM core as the instance_post_init function
    // for class T
    //
    // FIXME: it's not really guaranteed that there are no backpointers to
    // obj; it's quite possible that they have been created by instance_init().
    // The receiver should be &self, not &mut self.
    T::INSTANCE_POST_INIT.unwrap()(unsafe { &mut *obj.cast::<T>() })
}

/// Trait exposed by all structs corresponding to QOM objects.
///
/// # Safety
///
/// For classes declared in C:
///
/// - `Class` and `TYPE` must match the data in the `TypeInfo`;
///
/// - the first field of the struct must be of the instance type corresponding
///   to the superclass, as declared in the `TypeInfo`
///
/// - likewise, the first field of the `Class` struct must be of the class type
///   corresponding to the superclass
///
/// For classes declared in Rust and implementing [`ObjectImpl`]:
///
/// - the struct must be `#[repr(C)]`;
///
/// - the first field of the struct must be of the instance struct corresponding
///   to the superclass, which is `ObjectImpl::ParentType`
///
/// - likewise, the first field of the `Class` must be of the class struct
///   corresponding to the superclass, which is `ObjectImpl::ParentType::Class`.
pub unsafe trait ObjectType: Sized {
    /// The QOM class object corresponding to this struct.  Not used yet.
    type Class;

    /// The name of the type, which can be passed to `object_new()` to
    /// generate an instance of this type.
    const TYPE_NAME: &'static CStr;
}

/// Trait a type must implement to be registered with QEMU.
pub trait ObjectImpl: ObjectType + ClassInitImpl {
    /// The parent of the type.  This should match the first field of
    /// the struct that implements `ObjectImpl`:
    type ParentType: ObjectType;

    /// Whether the object can be instantiated
    const ABSTRACT: bool = false;
    const INSTANCE_FINALIZE: Option<unsafe extern "C" fn(obj: *mut Object)> = None;

    /// Function that is called to initialize an object.  The parent class will
    /// have already been initialized so the type is only responsible for
    /// initializing its own members.
    ///
    /// FIXME: The argument is not really a valid reference. `&mut
    /// MaybeUninit<Self>` would be a better description.
    const INSTANCE_INIT: Option<unsafe fn(&mut Self)> = None;

    /// Function that is called to finish initialization of an object, once
    /// `INSTANCE_INIT` functions have been called.
    const INSTANCE_POST_INIT: Option<fn(&mut Self)> = None;

    const TYPE_INFO: TypeInfo = TypeInfo {
        name: Self::TYPE_NAME.as_ptr(),
        parent: Self::ParentType::TYPE_NAME.as_ptr(),
        instance_size: core::mem::size_of::<Self>(),
        instance_align: core::mem::align_of::<Self>(),
        instance_init: match Self::INSTANCE_INIT {
            None => None,
            Some(_) => Some(rust_instance_init::<Self>),
        },
        instance_post_init: match Self::INSTANCE_POST_INIT {
            None => None,
            Some(_) => Some(rust_instance_post_init::<Self>),
        },
        instance_finalize: Self::INSTANCE_FINALIZE,
        abstract_: Self::ABSTRACT,
        class_size: core::mem::size_of::<Self::Class>(),
        class_init: <Self as ClassInitImpl>::CLASS_INIT,
        class_base_init: <Self as ClassInitImpl>::CLASS_BASE_INIT,
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
/// like [`ObjectImpl`] or [`DeviceImpl`](crate::device_class::DeviceImpl).
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
