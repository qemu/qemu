// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Helper macros to declare migration state for device models.
//!
//! Some macros are direct equivalents to the C macros declared in
//! `include/migration/vmstate.h` while
//! [`vmstate_subsections`](crate::vmstate_subsections) and
//! [`vmstate_fields`](crate::vmstate_fields) are meant to be used when
//! declaring a device model state struct.

#[doc(alias = "VMSTATE_UNUSED_BUFFER")]
#[macro_export]
macro_rules! vmstate_unused_buffer {
    ($field_exists_fn:expr, $version_id:expr, $size:expr) => {{
        $crate::bindings::VMStateField {
            name: c_str!("unused").as_ptr(),
            err_hint: ::core::ptr::null(),
            offset: 0,
            size: $size,
            start: 0,
            num: 0,
            num_offset: 0,
            size_offset: 0,
            info: unsafe { ::core::ptr::addr_of!($crate::bindings::vmstate_info_unused_buffer) },
            flags: VMStateFlags::VMS_BUFFER,
            vmsd: ::core::ptr::null(),
            version_id: $version_id,
            struct_version_id: 0,
            field_exists: $field_exists_fn,
        }
    }};
}

#[doc(alias = "VMSTATE_UNUSED_V")]
#[macro_export]
macro_rules! vmstate_unused_v {
    ($version_id:expr, $size:expr) => {{
        $crate::vmstate_unused_buffer!(None, $version_id, $size)
    }};
}

#[doc(alias = "VMSTATE_UNUSED")]
#[macro_export]
macro_rules! vmstate_unused {
    ($size:expr) => {{
        $crate::vmstate_unused_v!(0, $size)
    }};
}

#[doc(alias = "VMSTATE_SINGLE_TEST")]
#[macro_export]
macro_rules! vmstate_single_test {
    ($field_name:ident, $struct_name:ty, $field_exists_fn:expr, $version_id:expr, $info:expr, $size:expr) => {{
        $crate::bindings::VMStateField {
            name: ::core::concat!(::core::stringify!($field_name), 0)
                .as_bytes()
                .as_ptr() as *const ::std::os::raw::c_char,
            err_hint: ::core::ptr::null(),
            offset: $crate::offset_of!($struct_name, $field_name),
            size: $size,
            start: 0,
            num: 0,
            num_offset: 0,
            size_offset: 0,
            info: unsafe { $info },
            flags: VMStateFlags::VMS_SINGLE,
            vmsd: ::core::ptr::null(),
            version_id: $version_id,
            struct_version_id: 0,
            field_exists: $field_exists_fn,
        }
    }};
}

#[doc(alias = "VMSTATE_SINGLE")]
#[macro_export]
macro_rules! vmstate_single {
    ($field_name:ident, $struct_name:ty, $version_id:expr, $info:expr, $size:expr) => {{
        $crate::vmstate_single_test!($field_name, $struct_name, None, $version_id, $info, $size)
    }};
}

#[doc(alias = "VMSTATE_UINT32_V")]
#[macro_export]
macro_rules! vmstate_uint32_v {
    ($field_name:ident, $struct_name:ty, $version_id:expr) => {{
        $crate::vmstate_single!(
            $field_name,
            $struct_name,
            $version_id,
            ::core::ptr::addr_of!($crate::bindings::vmstate_info_uint32),
            ::core::mem::size_of::<u32>()
        )
    }};
}

#[doc(alias = "VMSTATE_UINT32")]
#[macro_export]
macro_rules! vmstate_uint32 {
    ($field_name:ident, $struct_name:ty) => {{
        $crate::vmstate_uint32_v!($field_name, $struct_name, 0)
    }};
}

#[doc(alias = "VMSTATE_INT32_V")]
#[macro_export]
macro_rules! vmstate_int32_v {
    ($field_name:ident, $struct_name:ty, $version_id:expr) => {{
        $crate::vmstate_single!(
            $field_name,
            $struct_name,
            $version_id,
            ::core::ptr::addr_of!($crate::bindings::vmstate_info_int32),
            ::core::mem::size_of::<i32>()
        )
    }};
}

#[doc(alias = "VMSTATE_INT32")]
#[macro_export]
macro_rules! vmstate_int32 {
    ($field_name:ident, $struct_name:ty) => {{
        $crate::vmstate_int32_v!($field_name, $struct_name, 0)
    }};
}

#[doc(alias = "VMSTATE_ARRAY")]
#[macro_export]
macro_rules! vmstate_array {
    ($field_name:ident, $struct_name:ty, $length:expr, $version_id:expr, $info:expr, $size:expr) => {{
        $crate::bindings::VMStateField {
            name: ::core::concat!(::core::stringify!($field_name), 0)
                .as_bytes()
                .as_ptr() as *const ::std::os::raw::c_char,
            err_hint: ::core::ptr::null(),
            offset: $crate::offset_of!($struct_name, $field_name),
            size: $size,
            start: 0,
            num: $length as _,
            num_offset: 0,
            size_offset: 0,
            info: unsafe { $info },
            flags: VMStateFlags::VMS_ARRAY,
            vmsd: ::core::ptr::null(),
            version_id: $version_id,
            struct_version_id: 0,
            field_exists: None,
        }
    }};
}

#[doc(alias = "VMSTATE_UINT32_ARRAY_V")]
#[macro_export]
macro_rules! vmstate_uint32_array_v {
    ($field_name:ident, $struct_name:ty, $length:expr, $version_id:expr) => {{
        $crate::vmstate_array!(
            $field_name,
            $struct_name,
            $length,
            $version_id,
            ::core::ptr::addr_of!($crate::bindings::vmstate_info_uint32),
            ::core::mem::size_of::<u32>()
        )
    }};
}

#[doc(alias = "VMSTATE_UINT32_ARRAY")]
#[macro_export]
macro_rules! vmstate_uint32_array {
    ($field_name:ident, $struct_name:ty, $length:expr) => {{
        $crate::vmstate_uint32_array_v!($field_name, $struct_name, $length, 0)
    }};
}

#[doc(alias = "VMSTATE_STRUCT_POINTER_V")]
#[macro_export]
macro_rules! vmstate_struct_pointer_v {
    ($field_name:ident, $struct_name:ty, $version_id:expr, $vmsd:expr, $type:ty) => {{
        $crate::bindings::VMStateField {
            name: ::core::concat!(::core::stringify!($field_name), 0)
                .as_bytes()
                .as_ptr() as *const ::std::os::raw::c_char,
            err_hint: ::core::ptr::null(),
            offset: $crate::offset_of!($struct_name, $field_name),
            size: ::core::mem::size_of::<*const $type>(),
            start: 0,
            num: 0,
            num_offset: 0,
            size_offset: 0,
            info: ::core::ptr::null(),
            flags: VMStateFlags(VMStateFlags::VMS_STRUCT.0 | VMStateFlags::VMS_POINTER.0),
            vmsd: unsafe { $vmsd },
            version_id: $version_id,
            struct_version_id: 0,
            field_exists: None,
        }
    }};
}

#[doc(alias = "VMSTATE_ARRAY_OF_POINTER")]
#[macro_export]
macro_rules! vmstate_array_of_pointer {
    ($field_name:ident, $struct_name:ty, $num:expr, $version_id:expr, $info:expr, $type:ty) => {{
        $crate::bindings::VMStateField {
            name: ::core::concat!(::core::stringify!($field_name), 0)
                .as_bytes()
                .as_ptr() as *const ::std::os::raw::c_char,
            version_id: $version_id,
            num: $num as _,
            info: unsafe { $info },
            size: ::core::mem::size_of::<*const $type>(),
            flags: VMStateFlags(VMStateFlags::VMS_ARRAY.0 | VMStateFlags::VMS_ARRAY_OF_POINTER.0),
            offset: $crate::offset_of!($struct_name, $field_name),
            err_hint: ::core::ptr::null(),
            start: 0,
            num_offset: 0,
            size_offset: 0,
            vmsd: ::core::ptr::null(),
            struct_version_id: 0,
            field_exists: None,
        }
    }};
}

#[doc(alias = "VMSTATE_ARRAY_OF_POINTER_TO_STRUCT")]
#[macro_export]
macro_rules! vmstate_array_of_pointer_to_struct {
    ($field_name:ident, $struct_name:ty, $num:expr, $version_id:expr, $vmsd:expr, $type:ty) => {{
        $crate::bindings::VMStateField {
            name: ::core::concat!(::core::stringify!($field_name), 0)
                .as_bytes()
                .as_ptr() as *const ::std::os::raw::c_char,
            version_id: $version_id,
            num: $num as _,
            vmsd: unsafe { $vmsd },
            size: ::core::mem::size_of::<*const $type>(),
            flags: VMStateFlags(
                VMStateFlags::VMS_ARRAY.0
                    | VMStateFlags::VMS_STRUCT.0
                    | VMStateFlags::VMS_ARRAY_OF_POINTER.0,
            ),
            offset: $crate::offset_of!($struct_name, $field_name),
            err_hint: ::core::ptr::null(),
            start: 0,
            num_offset: 0,
            size_offset: 0,
            vmsd: ::core::ptr::null(),
            struct_version_id: 0,
            field_exists: None,
        }
    }};
}

#[doc(alias = "VMSTATE_CLOCK_V")]
#[macro_export]
macro_rules! vmstate_clock_v {
    ($field_name:ident, $struct_name:ty, $version_id:expr) => {{
        $crate::vmstate_struct_pointer_v!(
            $field_name,
            $struct_name,
            $version_id,
            ::core::ptr::addr_of!($crate::bindings::vmstate_clock),
            $crate::bindings::Clock
        )
    }};
}

#[doc(alias = "VMSTATE_CLOCK")]
#[macro_export]
macro_rules! vmstate_clock {
    ($field_name:ident, $struct_name:ty) => {{
        $crate::vmstate_clock_v!($field_name, $struct_name, 0)
    }};
}

#[doc(alias = "VMSTATE_ARRAY_CLOCK_V")]
#[macro_export]
macro_rules! vmstate_array_clock_v {
    ($field_name:ident, $struct_name:ty, $num:expr, $version_id:expr) => {{
        $crate::vmstate_array_of_pointer_to_struct!(
            $field_name,
            $struct_name,
            $num,
            $version_id,
            ::core::ptr::addr_of!($crate::bindings::vmstate_clock),
            $crate::bindings::Clock
        )
    }};
}

#[doc(alias = "VMSTATE_ARRAY_CLOCK")]
#[macro_export]
macro_rules! vmstate_array_clock {
    ($field_name:ident, $struct_name:ty, $num:expr) => {{
        $crate::vmstate_array_clock_v!($field_name, $struct_name, $name, 0)
    }};
}

/// Helper macro to declare a list of
/// ([`VMStateField`](`crate::bindings::VMStateField`)) into a static and return
/// a pointer to the array of values it created.
#[macro_export]
macro_rules! vmstate_fields {
    ($($field:expr),*$(,)*) => {{
        static _FIELDS: &[$crate::bindings::VMStateField] = &[
            $($field),*,
            $crate::bindings::VMStateField {
                name: ::core::ptr::null(),
                err_hint: ::core::ptr::null(),
                offset: 0,
                size: 0,
                start: 0,
                num: 0,
                num_offset: 0,
                size_offset: 0,
                info: ::core::ptr::null(),
                flags: VMStateFlags::VMS_END,
                vmsd: ::core::ptr::null(),
                version_id: 0,
                struct_version_id: 0,
                field_exists: None,
            }
        ];
        _FIELDS.as_ptr()
    }}
}

/// A transparent wrapper type for the `subsections` field of
/// [`VMStateDescription`](crate::bindings::VMStateDescription).
///
/// This is necessary to be able to declare subsection descriptions as statics,
/// because the only way to implement `Sync` for a foreign type (and `*const`
/// pointers are foreign types in Rust) is to create a wrapper struct and
/// `unsafe impl Sync` for it.
///
/// This struct is used in the
/// [`vm_state_subsections`](crate::vmstate_subsections) macro implementation.
#[repr(transparent)]
pub struct VMStateSubsectionsWrapper(pub &'static [*const crate::bindings::VMStateDescription]);

unsafe impl Sync for VMStateSubsectionsWrapper {}

/// Helper macro to declare a list of subsections
/// ([`VMStateDescription`](`crate::bindings::VMStateDescription`)) into a
/// static and return a pointer to the array of pointers it created.
#[macro_export]
macro_rules! vmstate_subsections {
    ($($subsection:expr),*$(,)*) => {{
        static _SUBSECTIONS: $crate::vmstate::VMStateSubsectionsWrapper = $crate::vmstate::VMStateSubsectionsWrapper(&[
            $({
                static _SUBSECTION: $crate::bindings::VMStateDescription = $subsection;
                ::core::ptr::addr_of!(_SUBSECTION)
            }),*,
            ::core::ptr::null()
        ]);
        _SUBSECTIONS.0.as_ptr()
    }}
}
