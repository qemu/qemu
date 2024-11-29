// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::ffi::CStr;

use qemu_api::{
    bindings::*, c_str, declare_properties, define_property, definitions::ObjectImpl,
    device_class::DeviceImpl, impl_device_class, prelude::*, zeroable::Zeroable,
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

    unsafe impl ObjectType for DummyState {
        type Class = <DeviceState as ObjectType>::Class;
        const TYPE_NAME: &'static CStr = c_str!("dummy");
    }

    impl ObjectImpl for DummyState {
        type ParentType = DeviceState;
        const ABSTRACT: bool = false;
    }

    impl DeviceImpl for DummyState {
        fn properties() -> &'static [Property] {
            &DUMMY_PROPERTIES
        }
        fn vmsd() -> Option<&'static VMStateDescription> {
            Some(&VMSTATE)
        }
    }

    unsafe {
        module_call_init(module_init_type::MODULE_INIT_QOM);
        object_unref(object_new(DummyState::TYPE_NAME.as_ptr()).cast());
    }
}
