// Copyright (C) 2025 Intel Corporation.
// Author(s): Zhao Liu <zhai1.liu@intel.com>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::{ffi::CStr, mem::size_of, slice};

use qemu_api::{
    bindings::{vmstate_info_int8, vmstate_info_uint8, vmstate_info_unused_buffer, VMStateFlags},
    c_str,
    vmstate::{VMStateDescription, VMStateField},
    vmstate_fields, vmstate_of, vmstate_unused,
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
