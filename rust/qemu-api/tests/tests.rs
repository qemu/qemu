// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::{
    ffi::CStr,
    os::raw::c_void,
    ptr::{addr_of, addr_of_mut},
};

use qemu_api::{
    bindings::*,
    c_str,
    cell::{self, BqlCell},
    declare_properties, define_property,
    prelude::*,
    qdev::{DeviceClass, DeviceImpl, DeviceState, Property},
    qom::{ClassInitImpl, ObjectImpl, ParentField},
    vmstate::VMStateDescription,
    zeroable::Zeroable,
};

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
    parent: ParentField<DeviceState>,
    migrate_clock: bool,
}

qom_isa!(DummyState: Object, DeviceState);

pub struct DummyClass {
    parent_class: <DeviceState as ObjectType>::Class,
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
    type Class = DummyClass;
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

// `impl<T> ClassInitImpl<DummyClass> for T` doesn't work since it violates
// orphan rule.
impl ClassInitImpl<DummyClass> for DummyState {
    fn class_init(klass: &mut DummyClass) {
        <Self as ClassInitImpl<DeviceClass>>::class_init(&mut klass.parent_class);
    }
}

#[derive(qemu_api_macros::offsets)]
#[repr(C)]
#[derive(qemu_api_macros::Object)]
pub struct DummyChildState {
    parent: ParentField<DummyState>,
}

qom_isa!(DummyChildState: Object, DeviceState, DummyState);

pub struct DummyChildClass {
    parent_class: <DummyState as ObjectType>::Class,
}

unsafe impl ObjectType for DummyChildState {
    type Class = DummyChildClass;
    const TYPE_NAME: &'static CStr = c_str!("dummy_child");
}

impl ObjectImpl for DummyChildState {
    type ParentType = DummyState;
    const ABSTRACT: bool = false;
}

impl DeviceImpl for DummyChildState {}

impl ClassInitImpl<DummyClass> for DummyChildState {
    fn class_init(klass: &mut DummyClass) {
        <Self as ClassInitImpl<DeviceClass>>::class_init(&mut klass.parent_class);
    }
}

impl ClassInitImpl<DummyChildClass> for DummyChildState {
    fn class_init(klass: &mut DummyChildClass) {
        <Self as ClassInitImpl<DummyClass>>::class_init(&mut klass.parent_class);
    }
}

fn init_qom() {
    static ONCE: BqlCell<bool> = BqlCell::new(false);

    cell::bql_start_test();
    if !ONCE.get() {
        unsafe {
            module_call_init(module_init_type::MODULE_INIT_QOM);
        }
        ONCE.set(true);
    }
}

#[test]
/// Create and immediately drop an instance.
fn test_object_new() {
    init_qom();
    unsafe {
        object_unref(object_new(DummyState::TYPE_NAME.as_ptr()).cast());
        object_unref(object_new(DummyChildState::TYPE_NAME.as_ptr()).cast());
    }
}

#[test]
/// Try invoking a method on an object.
fn test_typename() {
    init_qom();
    let p: *mut DummyState = unsafe { object_new(DummyState::TYPE_NAME.as_ptr()).cast() };
    let p_ref: &DummyState = unsafe { &*p };
    assert_eq!(p_ref.typename(), "dummy");
    unsafe {
        object_unref(p_ref.as_object_mut_ptr().cast::<c_void>());
    }
}

// a note on all "cast" tests: usually, especially for downcasts the desired
// class would be placed on the right, for example:
//
//    let sbd_ref = p.dynamic_cast::<SysBusDevice>();
//
// Here I am doing the opposite to check that the resulting type is correct.

#[test]
#[allow(clippy::shadow_unrelated)]
/// Test casts on shared references.
fn test_cast() {
    init_qom();
    let p: *mut DummyState = unsafe { object_new(DummyState::TYPE_NAME.as_ptr()).cast() };

    let p_ref: &DummyState = unsafe { &*p };
    let obj_ref: &Object = p_ref.upcast();
    assert_eq!(addr_of!(*obj_ref), p.cast());

    let sbd_ref: Option<&SysBusDevice> = obj_ref.dynamic_cast();
    assert!(sbd_ref.is_none());

    let dev_ref: Option<&DeviceState> = obj_ref.downcast();
    assert_eq!(addr_of!(*dev_ref.unwrap()), p.cast());

    // SAFETY: the cast is wrong, but the value is only used for comparison
    unsafe {
        let sbd_ref: &SysBusDevice = obj_ref.unsafe_cast();
        assert_eq!(addr_of!(*sbd_ref), p.cast());

        object_unref(p_ref.as_object_mut_ptr().cast::<c_void>());
    }
}

#[test]
#[allow(clippy::shadow_unrelated)]
/// Test casts on mutable references.
fn test_cast_mut() {
    init_qom();
    let p: *mut DummyState = unsafe { object_new(DummyState::TYPE_NAME.as_ptr()).cast() };

    let p_ref: &mut DummyState = unsafe { &mut *p };
    let obj_ref: &mut Object = p_ref.upcast_mut();
    assert_eq!(addr_of_mut!(*obj_ref), p.cast());

    let sbd_ref: Result<&mut SysBusDevice, &mut Object> = obj_ref.dynamic_cast_mut();
    let obj_ref = sbd_ref.unwrap_err();

    let dev_ref: Result<&mut DeviceState, &mut Object> = obj_ref.downcast_mut();
    let dev_ref = dev_ref.unwrap();
    assert_eq!(addr_of_mut!(*dev_ref), p.cast());

    // SAFETY: the cast is wrong, but the value is only used for comparison
    unsafe {
        let sbd_ref: &mut SysBusDevice = obj_ref.unsafe_cast_mut();
        assert_eq!(addr_of_mut!(*sbd_ref), p.cast());

        object_unref(p_ref.as_object_mut_ptr().cast::<c_void>());
    }
}
