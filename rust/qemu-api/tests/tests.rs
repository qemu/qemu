// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::{ffi::CStr, os::raw::c_void};

use qemu_api::{
    bindings::*,
    c_str, declare_properties, define_property,
    definitions::{Class, ObjectImpl},
    device_class, device_class_init,
    zeroable::Zeroable,
};

#[test]
fn test_device_decl_macros() {
    // Test that macros can compile.
    pub static VMSTATE: VMStateDescription = VMStateDescription {
        name: c_str!("name").as_ptr(),
        unmigratable: true,
        ..Zeroable::ZERO
    };

    #[derive(qemu_api_macros::offsets)]
    #[repr(C)]
    #[derive(qemu_api_macros::Object)]
    pub struct DummyState {
        pub _parent: DeviceState,
        pub migrate_clock: bool,
    }

    #[repr(C)]
    pub struct DummyClass {
        pub _parent: DeviceClass,
    }

    declare_properties! {
        DUMMY_PROPERTIES,
            define_property!(
                c_str!("migrate-clk"),
                DummyState,
                migrate_clock,
                unsafe { &qdev_prop_bool },
                bool
            ),
    }

    device_class_init! {
        dummy_class_init,
        props => DUMMY_PROPERTIES,
        realize_fn => None,
        legacy_reset_fn => None,
        vmsd => VMSTATE,
    }

    impl ObjectImpl for DummyState {
        type Class = DummyClass;
        const TYPE_INFO: qemu_api::bindings::TypeInfo = qemu_api::type_info! { Self };
        const TYPE_NAME: &'static CStr = c_str!("dummy");
        const PARENT_TYPE_NAME: Option<&'static CStr> = Some(device_class::TYPE_DEVICE);
        const ABSTRACT: bool = false;
        const INSTANCE_INIT: Option<unsafe extern "C" fn(obj: *mut Object)> = None;
        const INSTANCE_POST_INIT: Option<unsafe extern "C" fn(obj: *mut Object)> = None;
        const INSTANCE_FINALIZE: Option<unsafe extern "C" fn(obj: *mut Object)> = None;
    }

    impl Class for DummyClass {
        const CLASS_INIT: Option<unsafe extern "C" fn(klass: *mut ObjectClass, data: *mut c_void)> =
            Some(dummy_class_init);
        const CLASS_BASE_INIT: Option<
            unsafe extern "C" fn(klass: *mut ObjectClass, data: *mut c_void),
        > = None;
    }

    unsafe {
        module_call_init(module_init_type::MODULE_INIT_QOM);
        object_unref(object_new(DummyState::TYPE_NAME.as_ptr()) as *mut _);
    }
}
