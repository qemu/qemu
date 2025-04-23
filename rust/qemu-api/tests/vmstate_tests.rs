// Copyright (C) 2025 Intel Corporation.
// Author(s): Zhao Liu <zhai1.liu@intel.com>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::{ffi::CStr, mem::size_of, os::raw::c_void, ptr::NonNull, slice};

use qemu_api::{
    bindings::{
        vmstate_info_bool, vmstate_info_int32, vmstate_info_int64, vmstate_info_int8,
        vmstate_info_uint64, vmstate_info_uint8, vmstate_info_unused_buffer, VMStateFlags,
    },
    c_str,
    cell::{BqlCell, Opaque},
    impl_vmstate_forward,
    vmstate::{VMStateDescription, VMStateField},
    vmstate_fields, vmstate_of, vmstate_struct, vmstate_unused, vmstate_validate,
    zeroable::Zeroable,
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
#[derive(qemu_api_macros::offsets)]
struct FooA {
    arr: [u8; FOO_ARRAY_MAX],
    num: u16,
    arr_mul: [i8; FOO_ARRAY_MAX],
    num_mul: u32,
    elem: i8,
}

static VMSTATE_FOOA: VMStateDescription = VMStateDescription {
    name: c_str!("foo_a").as_ptr(),
    version_id: 1,
    minimum_version_id: 1,
    fields: vmstate_fields! {
        vmstate_of!(FooA, elem),
        vmstate_unused!(size_of::<i64>()),
        vmstate_of!(FooA, arr[0 .. num]).with_version_id(0),
        vmstate_of!(FooA, arr_mul[0 .. num_mul * 16]),
    },
    ..Zeroable::ZERO
};

#[test]
fn test_vmstate_uint16() {
    let foo_fields: &[VMStateField] = unsafe { slice::from_raw_parts(VMSTATE_FOOA.fields, 5) };

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
    let foo_fields: &[VMStateField] = unsafe { slice::from_raw_parts(VMSTATE_FOOA.fields, 5) };

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
    let foo_fields: &[VMStateField] = unsafe { slice::from_raw_parts(VMSTATE_FOOA.fields, 5) };

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
    let foo_fields: &[VMStateField] = unsafe { slice::from_raw_parts(VMSTATE_FOOA.fields, 5) };

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
#[repr(C)]
#[derive(qemu_api_macros::offsets)]
struct FooB {
    arr_a: [FooA; FOO_ARRAY_MAX],
    num_a: u8,
    arr_a_mul: [FooA; FOO_ARRAY_MAX],
    num_a_mul: u32,
    wrap: BqlCell<u64>,
    val: bool,
    // FIXME: Use Timer array. Now we can't since it's hard to link savevm.c to test.
    arr_i64: [i64; FOO_ARRAY_MAX],
}

static VMSTATE_FOOB: VMStateDescription = VMStateDescription {
    name: c_str!("foo_b").as_ptr(),
    version_id: 2,
    minimum_version_id: 1,
    fields: vmstate_fields! {
        vmstate_of!(FooB, val).with_version_id(2),
        vmstate_of!(FooB, wrap),
        vmstate_struct!(FooB, arr_a[0 .. num_a], &VMSTATE_FOOA, FooA).with_version_id(1),
        vmstate_struct!(FooB, arr_a_mul[0 .. num_a_mul * 32], &VMSTATE_FOOA, FooA).with_version_id(2),
        vmstate_of!(FooB, arr_i64),
    },
    ..Zeroable::ZERO
};

#[test]
fn test_vmstate_bool_v() {
    let foo_fields: &[VMStateField] = unsafe { slice::from_raw_parts(VMSTATE_FOOB.fields, 6) };

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
    let foo_fields: &[VMStateField] = unsafe { slice::from_raw_parts(VMSTATE_FOOB.fields, 6) };

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
    let foo_fields: &[VMStateField] = unsafe { slice::from_raw_parts(VMSTATE_FOOB.fields, 6) };

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
    assert_eq!(foo_fields[2].vmsd, &VMSTATE_FOOA);
    assert!(foo_fields[2].field_exists.is_none());
}

#[test]
fn test_vmstate_struct_varray_uint32_multiply() {
    let foo_fields: &[VMStateField] = unsafe { slice::from_raw_parts(VMSTATE_FOOB.fields, 6) };

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
    assert_eq!(foo_fields[3].vmsd, &VMSTATE_FOOA);
    assert!(foo_fields[3].field_exists.is_none());
}

#[test]
fn test_vmstate_macro_array() {
    let foo_fields: &[VMStateField] = unsafe { slice::from_raw_parts(VMSTATE_FOOB.fields, 6) };

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

    // The last VMStateField in VMSTATE_FOOB.
    assert_eq!(foo_fields[5].flags, VMStateFlags::VMS_END);
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
#[derive(qemu_api_macros::offsets)]
struct FooC {
    ptr: *const i32,
    ptr_a: NonNull<FooA>,
    arr_ptr: [Box<u8>; FOO_ARRAY_MAX],
    arr_ptr_wrap: FooCWrapper,
}

static VMSTATE_FOOC: VMStateDescription = VMStateDescription {
    name: c_str!("foo_c").as_ptr(),
    version_id: 3,
    minimum_version_id: 1,
    fields: vmstate_fields! {
        vmstate_of!(FooC, ptr).with_version_id(2),
        // FIXME: Currently vmstate_struct doesn't support the pointer to structure.
        // VMSTATE_STRUCT_POINTER: vmstate_struct!(FooC, ptr_a, VMSTATE_FOOA, NonNull<FooA>)
        vmstate_unused!(size_of::<NonNull<FooA>>()),
        vmstate_of!(FooC, arr_ptr),
        vmstate_of!(FooC, arr_ptr_wrap),
    },
    ..Zeroable::ZERO
};

const PTR_SIZE: usize = size_of::<*mut ()>();

#[test]
fn test_vmstate_pointer() {
    let foo_fields: &[VMStateField] = unsafe { slice::from_raw_parts(VMSTATE_FOOC.fields, 6) };

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
fn test_vmstate_macro_array_of_pointer() {
    let foo_fields: &[VMStateField] = unsafe { slice::from_raw_parts(VMSTATE_FOOC.fields, 6) };

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
    let foo_fields: &[VMStateField] = unsafe { slice::from_raw_parts(VMSTATE_FOOC.fields, 6) };

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

// Add more member fields when vmstate_of/vmstate_struct support "test"
// parameter.
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

static VMSTATE_FOOD: VMStateDescription = VMStateDescription {
    name: c_str!("foo_d").as_ptr(),
    version_id: 3,
    minimum_version_id: 1,
    fields: vmstate_fields! {
        vmstate_validate!(FooD, c_str!("foo_d_0"), FooD::validate_food_0),
        vmstate_validate!(FooD, c_str!("foo_d_1"), FooD::validate_food_1),
        vmstate_validate!(FooD, c_str!("foo_d_2"), validate_food_2),
    },
    ..Zeroable::ZERO
};

#[test]
fn test_vmstate_validate() {
    let foo_fields: &[VMStateField] = unsafe { slice::from_raw_parts(VMSTATE_FOOD.fields, 4) };
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
