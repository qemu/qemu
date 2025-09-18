// Copyright (C) 2025 Intel Corporation.
// Author(s): Zhao Liu <zhao1.liu@intel.com>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::{
    ffi::{c_void, CStr},
    mem::size_of,
    ptr::NonNull,
    slice,
};

use bql::BqlCell;
use common::Opaque;
use migration::{
    bindings::{
        vmstate_info_bool, vmstate_info_int32, vmstate_info_int64, vmstate_info_int8,
        vmstate_info_uint64, vmstate_info_uint8, vmstate_info_unused_buffer, VMStateFlags,
    },
    impl_vmstate_forward, impl_vmstate_struct,
    vmstate::{VMStateDescription, VMStateDescriptionBuilder, VMStateField},
    vmstate_fields, vmstate_of, vmstate_unused, vmstate_validate,
};

const FOO_ARRAY_MAX: usize = 3;

// =========================== Test VMSTATE_FOOA ===========================
// Test the use cases of the vmstate macro, corresponding to the following C
// macro variants:
//   * VMSTATE_FOOA:
//     - VMSTATE_U16
//     - VMSTATE_UNUSED
//     - VMSTATE_VARRAY_UINT16_UNSAFE
//     - VMSTATE_VARRAY_MULTIPLY
#[repr(C)]
#[derive(Default)]
struct FooA {
    arr: [u8; FOO_ARRAY_MAX],
    num: u16,
    arr_mul: [i8; FOO_ARRAY_MAX],
    num_mul: u32,
    elem: i8,
}

static VMSTATE_FOOA: VMStateDescription<FooA> = VMStateDescriptionBuilder::<FooA>::new()
    .name(c"foo_a")
    .version_id(1)
    .minimum_version_id(1)
    .fields(vmstate_fields! {
        vmstate_of!(FooA, elem),
        vmstate_unused!(size_of::<i64>()),
        vmstate_of!(FooA, arr[0 .. num]).with_version_id(0),
        vmstate_of!(FooA, arr_mul[0 .. num_mul * 16]),
    })
    .build();

impl_vmstate_struct!(FooA, VMSTATE_FOOA);

#[test]
fn test_vmstate_uint16() {
    let foo_fields: &[VMStateField] =
        unsafe { slice::from_raw_parts(VMSTATE_FOOA.as_ref().fields, 5) };

    // 1st VMStateField ("elem") in VMSTATE_FOOA (corresponding to VMSTATE_UINT16)
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[0].name) }.to_bytes_with_nul(),
        b"elem\0"
    );
    assert_eq!(foo_fields[0].offset, 16);
    assert_eq!(foo_fields[0].num_offset, 0);
    assert_eq!(foo_fields[0].info, unsafe { &vmstate_info_int8 });
    assert_eq!(foo_fields[0].version_id, 0);
    assert_eq!(foo_fields[0].size, 1);
    assert_eq!(foo_fields[0].num, 0);
    assert_eq!(foo_fields[0].flags, VMStateFlags::VMS_SINGLE);
    assert!(foo_fields[0].vmsd.is_null());
    assert!(foo_fields[0].field_exists.is_none());
}

#[test]
fn test_vmstate_unused() {
    let foo_fields: &[VMStateField] =
        unsafe { slice::from_raw_parts(VMSTATE_FOOA.as_ref().fields, 5) };

    // 2nd VMStateField ("unused") in VMSTATE_FOOA (corresponding to VMSTATE_UNUSED)
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[1].name) }.to_bytes_with_nul(),
        b"unused\0"
    );
    assert_eq!(foo_fields[1].offset, 0);
    assert_eq!(foo_fields[1].num_offset, 0);
    assert_eq!(foo_fields[1].info, unsafe { &vmstate_info_unused_buffer });
    assert_eq!(foo_fields[1].version_id, 0);
    assert_eq!(foo_fields[1].size, 8);
    assert_eq!(foo_fields[1].num, 0);
    assert_eq!(foo_fields[1].flags, VMStateFlags::VMS_BUFFER);
    assert!(foo_fields[1].vmsd.is_null());
    assert!(foo_fields[1].field_exists.is_none());
}

#[test]
fn test_vmstate_varray_uint16_unsafe() {
    let foo_fields: &[VMStateField] =
        unsafe { slice::from_raw_parts(VMSTATE_FOOA.as_ref().fields, 5) };

    // 3rd VMStateField ("arr") in VMSTATE_FOOA (corresponding to
    // VMSTATE_VARRAY_UINT16_UNSAFE)
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[2].name) }.to_bytes_with_nul(),
        b"arr\0"
    );
    assert_eq!(foo_fields[2].offset, 0);
    assert_eq!(foo_fields[2].num_offset, 4);
    assert_eq!(foo_fields[2].info, unsafe { &vmstate_info_uint8 });
    assert_eq!(foo_fields[2].version_id, 0);
    assert_eq!(foo_fields[2].size, 1);
    assert_eq!(foo_fields[2].num, 0);
    assert_eq!(foo_fields[2].flags, VMStateFlags::VMS_VARRAY_UINT16);
    assert!(foo_fields[2].vmsd.is_null());
    assert!(foo_fields[2].field_exists.is_none());
}

#[test]
fn test_vmstate_varray_multiply() {
    let foo_fields: &[VMStateField] =
        unsafe { slice::from_raw_parts(VMSTATE_FOOA.as_ref().fields, 5) };

    // 4th VMStateField ("arr_mul") in VMSTATE_FOOA (corresponding to
    // VMSTATE_VARRAY_MULTIPLY)
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[3].name) }.to_bytes_with_nul(),
        b"arr_mul\0"
    );
    assert_eq!(foo_fields[3].offset, 6);
    assert_eq!(foo_fields[3].num_offset, 12);
    assert_eq!(foo_fields[3].info, unsafe { &vmstate_info_int8 });
    assert_eq!(foo_fields[3].version_id, 0);
    assert_eq!(foo_fields[3].size, 1);
    assert_eq!(foo_fields[3].num, 16);
    assert_eq!(
        foo_fields[3].flags.0,
        VMStateFlags::VMS_VARRAY_UINT32.0 | VMStateFlags::VMS_MULTIPLY_ELEMENTS.0
    );
    assert!(foo_fields[3].vmsd.is_null());
    assert!(foo_fields[3].field_exists.is_none());

    // The last VMStateField in VMSTATE_FOOA.
    assert_eq!(foo_fields[4].flags, VMStateFlags::VMS_END);
}

// =========================== Test VMSTATE_FOOB ===========================
// Test the use cases of the vmstate macro, corresponding to the following C
// macro variants:
//   * VMSTATE_FOOB:
//     - VMSTATE_BOOL_V
//     - VMSTATE_U64
//     - VMSTATE_STRUCT_VARRAY_UINT8
//     - (no C version) MULTIPLY variant of VMSTATE_STRUCT_VARRAY_UINT32
//     - VMSTATE_ARRAY
//     - VMSTATE_STRUCT_VARRAY_UINT8 with BqlCell wrapper & test_fn
#[repr(C)]
#[derive(Default)]
struct FooB {
    arr_a: [FooA; FOO_ARRAY_MAX],
    num_a: u8,
    arr_a_mul: [FooA; FOO_ARRAY_MAX],
    num_a_mul: u32,
    wrap: BqlCell<u64>,
    val: bool,
    // FIXME: Use Timer array. Now we can't since it's hard to link savevm.c to test.
    arr_i64: [i64; FOO_ARRAY_MAX],
    arr_a_wrap: [FooA; FOO_ARRAY_MAX],
    num_a_wrap: BqlCell<u32>,
}

fn validate_foob(_state: &FooB, _version_id: u8) -> bool {
    true
}

static VMSTATE_FOOB: VMStateDescription<FooB> = VMStateDescriptionBuilder::<FooB>::new()
    .name(c"foo_b")
    .version_id(2)
    .minimum_version_id(1)
    .fields(vmstate_fields! {
        vmstate_of!(FooB, val).with_version_id(2),
        vmstate_of!(FooB, wrap),
        vmstate_of!(FooB, arr_a[0 .. num_a]).with_version_id(1),
        vmstate_of!(FooB, arr_a_mul[0 .. num_a_mul * 32]).with_version_id(2),
        vmstate_of!(FooB, arr_i64),
        vmstate_of!(FooB, arr_a_wrap[0 .. num_a_wrap], validate_foob),
    })
    .build();

#[test]
fn test_vmstate_bool_v() {
    let foo_fields: &[VMStateField] =
        unsafe { slice::from_raw_parts(VMSTATE_FOOB.as_ref().fields, 7) };

    // 1st VMStateField ("val") in VMSTATE_FOOB (corresponding to VMSTATE_BOOL_V)
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[0].name) }.to_bytes_with_nul(),
        b"val\0"
    );
    assert_eq!(foo_fields[0].offset, 136);
    assert_eq!(foo_fields[0].num_offset, 0);
    assert_eq!(foo_fields[0].info, unsafe { &vmstate_info_bool });
    assert_eq!(foo_fields[0].version_id, 2);
    assert_eq!(foo_fields[0].size, 1);
    assert_eq!(foo_fields[0].num, 0);
    assert_eq!(foo_fields[0].flags, VMStateFlags::VMS_SINGLE);
    assert!(foo_fields[0].vmsd.is_null());
    assert!(foo_fields[0].field_exists.is_none());
}

#[test]
fn test_vmstate_uint64() {
    let foo_fields: &[VMStateField] =
        unsafe { slice::from_raw_parts(VMSTATE_FOOB.as_ref().fields, 7) };

    // 2nd VMStateField ("wrap") in VMSTATE_FOOB (corresponding to VMSTATE_U64)
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[1].name) }.to_bytes_with_nul(),
        b"wrap\0"
    );
    assert_eq!(foo_fields[1].offset, 128);
    assert_eq!(foo_fields[1].num_offset, 0);
    assert_eq!(foo_fields[1].info, unsafe { &vmstate_info_uint64 });
    assert_eq!(foo_fields[1].version_id, 0);
    assert_eq!(foo_fields[1].size, 8);
    assert_eq!(foo_fields[1].num, 0);
    assert_eq!(foo_fields[1].flags, VMStateFlags::VMS_SINGLE);
    assert!(foo_fields[1].vmsd.is_null());
    assert!(foo_fields[1].field_exists.is_none());
}

#[test]
fn test_vmstate_struct_varray_uint8() {
    let foo_fields: &[VMStateField] =
        unsafe { slice::from_raw_parts(VMSTATE_FOOB.as_ref().fields, 7) };

    // 3rd VMStateField ("arr_a") in VMSTATE_FOOB (corresponding to
    // VMSTATE_STRUCT_VARRAY_UINT8)
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[2].name) }.to_bytes_with_nul(),
        b"arr_a\0"
    );
    assert_eq!(foo_fields[2].offset, 0);
    assert_eq!(foo_fields[2].num_offset, 60);
    assert!(foo_fields[2].info.is_null()); // VMSTATE_STRUCT_VARRAY_UINT8 doesn't set info field.
    assert_eq!(foo_fields[2].version_id, 1);
    assert_eq!(foo_fields[2].size, 20);
    assert_eq!(foo_fields[2].num, 0);
    assert_eq!(
        foo_fields[2].flags.0,
        VMStateFlags::VMS_STRUCT.0 | VMStateFlags::VMS_VARRAY_UINT8.0
    );
    assert_eq!(foo_fields[2].vmsd, VMSTATE_FOOA.as_ref());
    assert!(foo_fields[2].field_exists.is_none());
}

#[test]
fn test_vmstate_struct_varray_uint32_multiply() {
    let foo_fields: &[VMStateField] =
        unsafe { slice::from_raw_parts(VMSTATE_FOOB.as_ref().fields, 7) };

    // 4th VMStateField ("arr_a_mul") in VMSTATE_FOOB (corresponding to
    // (no C version) MULTIPLY variant of VMSTATE_STRUCT_VARRAY_UINT32)
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[3].name) }.to_bytes_with_nul(),
        b"arr_a_mul\0"
    );
    assert_eq!(foo_fields[3].offset, 64);
    assert_eq!(foo_fields[3].num_offset, 124);
    assert!(foo_fields[3].info.is_null()); // VMSTATE_STRUCT_VARRAY_UINT8 doesn't set info field.
    assert_eq!(foo_fields[3].version_id, 2);
    assert_eq!(foo_fields[3].size, 20);
    assert_eq!(foo_fields[3].num, 32);
    assert_eq!(
        foo_fields[3].flags.0,
        VMStateFlags::VMS_STRUCT.0
            | VMStateFlags::VMS_VARRAY_UINT32.0
            | VMStateFlags::VMS_MULTIPLY_ELEMENTS.0
    );
    assert_eq!(foo_fields[3].vmsd, VMSTATE_FOOA.as_ref());
    assert!(foo_fields[3].field_exists.is_none());
}

#[test]
fn test_vmstate_macro_array() {
    let foo_fields: &[VMStateField] =
        unsafe { slice::from_raw_parts(VMSTATE_FOOB.as_ref().fields, 7) };

    // 5th VMStateField ("arr_i64") in VMSTATE_FOOB (corresponding to
    // VMSTATE_ARRAY)
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[4].name) }.to_bytes_with_nul(),
        b"arr_i64\0"
    );
    assert_eq!(foo_fields[4].offset, 144);
    assert_eq!(foo_fields[4].num_offset, 0);
    assert_eq!(foo_fields[4].info, unsafe { &vmstate_info_int64 });
    assert_eq!(foo_fields[4].version_id, 0);
    assert_eq!(foo_fields[4].size, 8);
    assert_eq!(foo_fields[4].num, FOO_ARRAY_MAX as i32);
    assert_eq!(foo_fields[4].flags, VMStateFlags::VMS_ARRAY);
    assert!(foo_fields[4].vmsd.is_null());
    assert!(foo_fields[4].field_exists.is_none());
}

#[test]
fn test_vmstate_struct_varray_uint8_wrapper() {
    let foo_fields: &[VMStateField] =
        unsafe { slice::from_raw_parts(VMSTATE_FOOB.as_ref().fields, 7) };
    let mut foo_b: FooB = Default::default();
    let foo_b_p = std::ptr::addr_of_mut!(foo_b).cast::<c_void>();

    // 6th VMStateField ("arr_a_wrap") in VMSTATE_FOOB (corresponding to
    // VMSTATE_STRUCT_VARRAY_UINT8). Other fields are checked in
    // test_vmstate_struct_varray_uint8.
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[5].name) }.to_bytes_with_nul(),
        b"arr_a_wrap\0"
    );
    assert_eq!(foo_fields[5].num_offset, 228);
    assert!(unsafe { foo_fields[5].field_exists.unwrap()(foo_b_p, 0) });

    // The last VMStateField in VMSTATE_FOOB.
    assert_eq!(foo_fields[6].flags, VMStateFlags::VMS_END);
}

// =========================== Test VMSTATE_FOOC ===========================
// Test the use cases of the vmstate macro, corresponding to the following C
// macro variants:
//   * VMSTATE_FOOC:
//     - VMSTATE_POINTER
//     - VMSTATE_ARRAY_OF_POINTER
struct FooCWrapper([Opaque<*mut u8>; FOO_ARRAY_MAX]); // Though Opaque<> array is almost impossible.

impl_vmstate_forward!(FooCWrapper);

#[repr(C)]
struct FooC {
    ptr: *const i32,
    ptr_a: NonNull<FooA>,
    arr_ptr: [Box<u8>; FOO_ARRAY_MAX],
    arr_ptr_wrap: FooCWrapper,
}

unsafe impl Sync for FooC {}

static VMSTATE_FOOC: VMStateDescription<FooC> = VMStateDescriptionBuilder::<FooC>::new()
    .name(c"foo_c")
    .version_id(3)
    .minimum_version_id(1)
    .fields(vmstate_fields! {
        vmstate_of!(FooC, ptr).with_version_id(2),
        vmstate_of!(FooC, ptr_a),
        vmstate_of!(FooC, arr_ptr),
        vmstate_of!(FooC, arr_ptr_wrap),
    })
    .build();

const PTR_SIZE: usize = size_of::<*mut ()>();

#[test]
fn test_vmstate_pointer() {
    let foo_fields: &[VMStateField] =
        unsafe { slice::from_raw_parts(VMSTATE_FOOC.as_ref().fields, 6) };

    // 1st VMStateField ("ptr") in VMSTATE_FOOC (corresponding to VMSTATE_POINTER)
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[0].name) }.to_bytes_with_nul(),
        b"ptr\0"
    );
    assert_eq!(foo_fields[0].offset, 0);
    assert_eq!(foo_fields[0].num_offset, 0);
    assert_eq!(foo_fields[0].info, unsafe { &vmstate_info_int32 });
    assert_eq!(foo_fields[0].version_id, 2);
    assert_eq!(foo_fields[0].size, 4);
    assert_eq!(foo_fields[0].num, 0);
    assert_eq!(
        foo_fields[0].flags.0,
        VMStateFlags::VMS_SINGLE.0 | VMStateFlags::VMS_POINTER.0
    );
    assert!(foo_fields[0].vmsd.is_null());
    assert!(foo_fields[0].field_exists.is_none());
}

#[test]
fn test_vmstate_struct_pointer() {
    let foo_fields: &[VMStateField] =
        unsafe { slice::from_raw_parts(VMSTATE_FOOC.as_ref().fields, 6) };

    // 2st VMStateField ("ptr_a") in VMSTATE_FOOC (corresponding to
    // VMSTATE_STRUCT_POINTER)
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[1].name) }.to_bytes_with_nul(),
        b"ptr_a\0"
    );
    assert_eq!(foo_fields[1].offset, PTR_SIZE);
    assert_eq!(foo_fields[1].num_offset, 0);
    assert_eq!(foo_fields[1].vmsd, VMSTATE_FOOA.as_ref());
    assert_eq!(foo_fields[1].version_id, 0);
    assert_eq!(foo_fields[1].size, size_of::<FooA>());
    assert_eq!(foo_fields[1].num, 0);
    assert_eq!(
        foo_fields[1].flags.0,
        VMStateFlags::VMS_STRUCT.0 | VMStateFlags::VMS_POINTER.0
    );
    assert!(foo_fields[1].info.is_null());
    assert!(foo_fields[1].field_exists.is_none());
}

#[test]
fn test_vmstate_macro_array_of_pointer() {
    let foo_fields: &[VMStateField] =
        unsafe { slice::from_raw_parts(VMSTATE_FOOC.as_ref().fields, 6) };

    // 3rd VMStateField ("arr_ptr") in VMSTATE_FOOC (corresponding to
    // VMSTATE_ARRAY_OF_POINTER)
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[2].name) }.to_bytes_with_nul(),
        b"arr_ptr\0"
    );
    assert_eq!(foo_fields[2].offset, 2 * PTR_SIZE);
    assert_eq!(foo_fields[2].num_offset, 0);
    assert_eq!(foo_fields[2].info, unsafe { &vmstate_info_uint8 });
    assert_eq!(foo_fields[2].version_id, 0);
    assert_eq!(foo_fields[2].size, PTR_SIZE);
    assert_eq!(foo_fields[2].num, FOO_ARRAY_MAX as i32);
    assert_eq!(
        foo_fields[2].flags.0,
        VMStateFlags::VMS_ARRAY.0 | VMStateFlags::VMS_ARRAY_OF_POINTER.0
    );
    assert!(foo_fields[2].vmsd.is_null());
    assert!(foo_fields[2].field_exists.is_none());
}

#[test]
fn test_vmstate_macro_array_of_pointer_wrapped() {
    let foo_fields: &[VMStateField] =
        unsafe { slice::from_raw_parts(VMSTATE_FOOC.as_ref().fields, 6) };

    // 4th VMStateField ("arr_ptr_wrap") in VMSTATE_FOOC (corresponding to
    // VMSTATE_ARRAY_OF_POINTER)
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[3].name) }.to_bytes_with_nul(),
        b"arr_ptr_wrap\0"
    );
    assert_eq!(foo_fields[3].offset, (FOO_ARRAY_MAX + 2) * PTR_SIZE);
    assert_eq!(foo_fields[3].num_offset, 0);
    assert_eq!(foo_fields[3].info, unsafe { &vmstate_info_uint8 });
    assert_eq!(foo_fields[3].version_id, 0);
    assert_eq!(foo_fields[3].size, PTR_SIZE);
    assert_eq!(foo_fields[3].num, FOO_ARRAY_MAX as i32);
    assert_eq!(
        foo_fields[3].flags.0,
        VMStateFlags::VMS_ARRAY.0 | VMStateFlags::VMS_ARRAY_OF_POINTER.0
    );
    assert!(foo_fields[3].vmsd.is_null());
    assert!(foo_fields[3].field_exists.is_none());

    // The last VMStateField in VMSTATE_FOOC.
    assert_eq!(foo_fields[4].flags, VMStateFlags::VMS_END);
}

// =========================== Test VMSTATE_FOOD ===========================
// Test the use cases of the vmstate macro, corresponding to the following C
// macro variants:
//   * VMSTATE_FOOD:
//     - VMSTATE_VALIDATE

// Add more member fields when vmstate_of support "test" parameter.
struct FooD;

impl FooD {
    fn validate_food_0(&self, _version_id: u8) -> bool {
        true
    }

    fn validate_food_1(_state: &FooD, _version_id: u8) -> bool {
        false
    }
}

fn validate_food_2(_state: &FooD, _version_id: u8) -> bool {
    true
}

static VMSTATE_FOOD: VMStateDescription<FooD> = VMStateDescriptionBuilder::<FooD>::new()
    .name(c"foo_d")
    .version_id(3)
    .minimum_version_id(1)
    .fields(vmstate_fields! {
        vmstate_validate!(FooD, c"foo_d_0", FooD::validate_food_0),
        vmstate_validate!(FooD, c"foo_d_1", FooD::validate_food_1),
        vmstate_validate!(FooD, c"foo_d_2", validate_food_2),
    })
    .build();

#[test]
fn test_vmstate_validate() {
    let foo_fields: &[VMStateField] =
        unsafe { slice::from_raw_parts(VMSTATE_FOOD.as_ref().fields, 4) };
    let mut foo_d = FooD;
    let foo_d_p = std::ptr::addr_of_mut!(foo_d).cast::<c_void>();

    // 1st VMStateField in VMSTATE_FOOD
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[0].name) }.to_bytes_with_nul(),
        b"foo_d_0\0"
    );
    assert_eq!(foo_fields[0].offset, 0);
    assert_eq!(foo_fields[0].num_offset, 0);
    assert!(foo_fields[0].info.is_null());
    assert_eq!(foo_fields[0].version_id, 0);
    assert_eq!(foo_fields[0].size, 0);
    assert_eq!(foo_fields[0].num, 0);
    assert_eq!(
        foo_fields[0].flags.0,
        VMStateFlags::VMS_ARRAY.0 | VMStateFlags::VMS_MUST_EXIST.0
    );
    assert!(foo_fields[0].vmsd.is_null());
    assert!(unsafe { foo_fields[0].field_exists.unwrap()(foo_d_p, 0) });

    // 2nd VMStateField in VMSTATE_FOOD
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[1].name) }.to_bytes_with_nul(),
        b"foo_d_1\0"
    );
    assert!(!unsafe { foo_fields[1].field_exists.unwrap()(foo_d_p, 1) });

    // 3rd VMStateField in VMSTATE_FOOD
    assert_eq!(
        unsafe { CStr::from_ptr(foo_fields[2].name) }.to_bytes_with_nul(),
        b"foo_d_2\0"
    );
    assert!(unsafe { foo_fields[2].field_exists.unwrap()(foo_d_p, 2) });

    // The last VMStateField in VMSTATE_FOOD.
    assert_eq!(foo_fields[3].flags, VMStateFlags::VMS_END);
}
