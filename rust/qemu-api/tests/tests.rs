// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::{ffi::CStr, ptr::addr_of};

use qemu_api::{
    bindings::{module_call_init, module_init_type, qdev_prop_bool},
    c_str,
    cell::{self, BqlCell},
    declare_properties, define_property,
    prelude::*,
    qdev::{DeviceImpl, DeviceState, Property, ResettablePhasesImpl},
    qom::{ObjectImpl, ParentField},
    sysbus::SysBusDevice,
    vmstate::VMStateDescription,
    zeroable::Zeroable,
};

mod vmstate_tests;

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

impl DummyClass {
    pub fn class_init<T: DeviceImpl>(self: &mut DummyClass) {
        self.parent_class.class_init::<T>();
    }
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
    const CLASS_INIT: fn(&mut DummyClass) = DummyClass::class_init::<Self>;
}

impl ResettablePhasesImpl for DummyState {}

impl DeviceImpl for DummyState {
    fn properties() -> &'static [Property] {
        &DUMMY_PROPERTIES
    }
    fn vmsd() -> Option<&'static VMStateDescription> {
        Some(&VMSTATE)
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
    const CLASS_INIT: fn(&mut DummyChildClass) = DummyChildClass::class_init::<Self>;
}

impl ResettablePhasesImpl for DummyChildState {}
impl DeviceImpl for DummyChildState {}

impl DummyChildClass {
    pub fn class_init<T: DeviceImpl>(self: &mut DummyChildClass) {
        self.parent_class.class_init::<T>();
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
    drop(DummyState::new());
    drop(DummyChildState::new());
}

#[test]
#[allow(clippy::redundant_clone)]
/// Create, clone and then drop an instance.
fn test_clone() {
    init_qom();
    let p = DummyState::new();
    assert_eq!(p.clone().typename(), "dummy");
    drop(p);
}

#[test]
/// Try invoking a method on an object.
fn test_typename() {
    init_qom();
    let p = DummyState::new();
    assert_eq!(p.typename(), "dummy");
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
    let p = DummyState::new();
    let p_ptr: *mut DummyState = p.as_mut_ptr();
    let p_ref: &mut DummyState = unsafe { &mut *p_ptr };

    let obj_ref: &Object = p_ref.upcast();
    assert_eq!(addr_of!(*obj_ref), p_ptr.cast());

    let sbd_ref: Option<&SysBusDevice> = obj_ref.dynamic_cast();
    assert!(sbd_ref.is_none());

    let dev_ref: Option<&DeviceState> = obj_ref.downcast();
    assert_eq!(addr_of!(*dev_ref.unwrap()), p_ptr.cast());

    // SAFETY: the cast is wrong, but the value is only used for comparison
    unsafe {
        let sbd_ref: &SysBusDevice = obj_ref.unsafe_cast();
        assert_eq!(addr_of!(*sbd_ref), p_ptr.cast());
    }
}
