// Copyright 2025, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use quote::quote;

use super::*;

macro_rules! derive_compile_fail {
    ($derive_fn:ident, $input:expr, $($error_msg:expr),+ $(,)?) => {{
        let input: proc_macro2::TokenStream = $input;
        let error_msg = &[$( quote! { ::core::compile_error! { $error_msg } } ),*];
        let derive_fn: fn(input: syn::DeriveInput) -> Result<proc_macro2::TokenStream, syn::Error> =
            $derive_fn;

        let input: syn::DeriveInput = syn::parse2(input).unwrap();
        let result = derive_fn(input);
        let err = result.unwrap_err().into_compile_error();
        assert_eq!(
            err.to_string(),
            quote! { #(#error_msg)* }.to_string()
        );
    }};
}

macro_rules! derive_compile {
    ($derive_fn:ident, $input:expr, $($expected:tt)*) => {{
        let input: proc_macro2::TokenStream = $input;
        let expected: proc_macro2::TokenStream = $($expected)*;
        let derive_fn: fn(input: syn::DeriveInput) -> Result<proc_macro2::TokenStream, syn::Error> =
            $derive_fn;

        let input: syn::DeriveInput = syn::parse2(input).unwrap();
        let result = derive_fn(input).unwrap();
        assert_eq!(result.to_string(), expected.to_string());
    }};
}

#[test]
fn test_derive_device() {
    // Check that repr(C) is used
    derive_compile_fail!(
        derive_device_or_error,
        quote! {
            #[derive(Device)]
            struct Foo {
                _unused: [u8; 0],
            }
        },
        "#[repr(C)] required for #[derive(Device)]"
    );
    // Check that invalid/misspelled attributes raise an error
    derive_compile_fail!(
        derive_device_or_error,
        quote! {
            #[repr(C)]
            #[derive(Device)]
            struct DummyState {
                #[property(defalt = true)]
                migrate_clock: bool,
            }
        },
        "Expected one of `bit`, `default` or `rename`"
    );
    // Check that repeated attributes are not allowed:
    derive_compile_fail!(
        derive_device_or_error,
        quote! {
            #[repr(C)]
            #[derive(Device)]
            struct DummyState {
                #[property(rename = "migrate-clk", rename = "migrate-clk", default = true)]
                migrate_clock: bool,
            }
        },
        "Duplicate argument",
        "Already used here",
    );
    derive_compile_fail!(
        derive_device_or_error,
        quote! {
            #[repr(C)]
            #[derive(Device)]
            struct DummyState {
                #[property(default = true, default = true)]
                migrate_clock: bool,
            }
        },
        "Duplicate argument",
        "Already used here",
    );
    derive_compile_fail!(
        derive_device_or_error,
        quote! {
            #[repr(C)]
            #[derive(Device)]
            struct DummyState {
                #[property(bit = 0, bit = 1)]
                flags: u32,
            }
        },
        "Duplicate argument",
        "Already used here",
    );
    // Check that the field name is preserved when `rename` isn't used:
    derive_compile!(
        derive_device_or_error,
        quote! {
            #[repr(C)]
            #[derive(Device)]
            pub struct DummyState {
                parent: ParentField<DeviceState>,
                #[property(default = true)]
                migrate_clock: bool,
            }
        },
        quote! {
            unsafe impl ::hwcore::DevicePropertiesImpl for DummyState {
                const PROPERTIES: &'static [::hwcore::bindings::Property] = &[
                    ::hwcore::bindings::Property {
                        name: ::std::ffi::CStr::as_ptr(c"migrate_clock"),
                        info: <bool as ::hwcore::QDevProp>::BASE_INFO,
                        offset: ::core::mem::offset_of!(DummyState, migrate_clock) as isize,
                        bitnr: 0,
                        set_default: true,
                        defval: ::hwcore::bindings::Property__bindgen_ty_1 { u: true as u64 },
                        ..::common::Zeroable::ZERO
                    }
                ];
            }
        }
    );
    // Check that `rename` value is used for the property name when used:
    derive_compile!(
        derive_device_or_error,
        quote! {
            #[repr(C)]
            #[derive(Device)]
            pub struct DummyState {
                parent: ParentField<DeviceState>,
                #[property(rename = "migrate-clk", default = true)]
                migrate_clock: bool,
            }
        },
        quote! {
            unsafe impl ::hwcore::DevicePropertiesImpl for DummyState {
                const PROPERTIES: &'static [::hwcore::bindings::Property] = &[
                    ::hwcore::bindings::Property {
                        name: ::std::ffi::CStr::as_ptr(c"migrate-clk"),
                        info: <bool as ::hwcore::QDevProp>::BASE_INFO,
                        offset: ::core::mem::offset_of!(DummyState, migrate_clock) as isize,
                        bitnr: 0,
                        set_default: true,
                        defval: ::hwcore::bindings::Property__bindgen_ty_1 { u: true as u64 },
                        ..::common::Zeroable::ZERO
                    }
                ];
            }
        }
    );
    // Check that `bit` value is used for the bit property without default
    // value (note: though C macro (e.g., DEFINE_PROP_BIT) always requires
    // default value, Rust side allows to default this field to "0"):
    derive_compile!(
        derive_device_or_error,
        quote! {
            #[repr(C)]
            #[derive(Device)]
            pub struct DummyState {
                parent: ParentField<DeviceState>,
                #[property(bit = 3)]
                flags: u32,
            }
        },
        quote! {
            unsafe impl ::hwcore::DevicePropertiesImpl for DummyState {
                const PROPERTIES: &'static [::hwcore::bindings::Property] = &[
                    ::hwcore::bindings::Property {
                        name: ::std::ffi::CStr::as_ptr(c"flags"),
                        info: <u32 as ::hwcore::QDevProp>::BIT_INFO,
                        offset: ::core::mem::offset_of!(DummyState, flags) as isize,
                        bitnr: 3,
                        set_default: false,
                        defval: ::hwcore::bindings::Property__bindgen_ty_1 { u: 0 as u64 },
                        ..::common::Zeroable::ZERO
                    }
                ];
            }
        }
    );
    // Check that `bit` value is used for the bit property when used:
    derive_compile!(
        derive_device_or_error,
        quote! {
            #[repr(C)]
            #[derive(Device)]
            pub struct DummyState {
                parent: ParentField<DeviceState>,
                #[property(bit = 3, default = true)]
                flags: u32,
            }
        },
        quote! {
            unsafe impl ::hwcore::DevicePropertiesImpl for DummyState {
                const PROPERTIES: &'static [::hwcore::bindings::Property] = &[
                    ::hwcore::bindings::Property {
                        name: ::std::ffi::CStr::as_ptr(c"flags"),
                        info: <u32 as ::hwcore::QDevProp>::BIT_INFO,
                        offset: ::core::mem::offset_of!(DummyState, flags) as isize,
                        bitnr: 3,
                        set_default: true,
                        defval: ::hwcore::bindings::Property__bindgen_ty_1 { u: true as u64 },
                        ..::common::Zeroable::ZERO
                    }
                ];
            }
        }
    );
    // Check that `bit` value is used for the bit property with rename when used:
    derive_compile!(
        derive_device_or_error,
        quote! {
            #[repr(C)]
            #[derive(Device)]
            pub struct DummyState {
                parent: ParentField<DeviceState>,
                #[property(rename = "msi", bit = 3, default = false)]
                flags: u64,
            }
        },
        quote! {
            unsafe impl ::hwcore::DevicePropertiesImpl for DummyState {
                const PROPERTIES: &'static [::hwcore::bindings::Property] = &[
                    ::hwcore::bindings::Property {
                        name: ::std::ffi::CStr::as_ptr(c"msi"),
                        info: <u64 as ::hwcore::QDevProp>::BIT_INFO,
                        offset: ::core::mem::offset_of!(DummyState, flags) as isize,
                        bitnr: 3,
                        set_default: true,
                        defval: ::hwcore::bindings::Property__bindgen_ty_1 { u: false as u64 },
                        ..::common::Zeroable::ZERO
                    }
                ];
            }
        }
    );
}

#[test]
fn test_derive_object() {
    derive_compile_fail!(
        derive_object_or_error,
        quote! {
            #[derive(Object)]
            struct Foo {
                _unused: [u8; 0],
            }
        },
        "#[repr(C)] required for #[derive(Object)]"
    );
    derive_compile!(
        derive_object_or_error,
        quote! {
            #[derive(Object)]
            #[repr(C)]
            struct Foo {
                _unused: [u8; 0],
            }
        },
        quote! {
            ::common::assert_field_type!(
                Foo,
                _unused,
                ::qom::ParentField<<Foo as ::qom::ObjectImpl>::ParentType>
            );
            ::util::module_init! {
                MODULE_INIT_QOM => unsafe {
                    ::qom::type_register_static(&<Foo as ::qom::ObjectImpl>::TYPE_INFO);
                }
            }
        }
    );
}

#[test]
fn test_derive_tryinto() {
    derive_compile_fail!(
        derive_tryinto_or_error,
        quote! {
            #[derive(TryInto)]
            struct Foo {
                _unused: [u8; 0],
            }
        },
        "#[repr(u8/u16/u32/u64) required for #[derive(TryInto)]"
    );
    derive_compile!(
        derive_tryinto_or_error,
        quote! {
            #[derive(TryInto)]
            #[repr(u8)]
            enum Foo {
                First = 0,
                Second,
            }
        },
        quote! {
            impl Foo {
                #[allow(dead_code)]
                pub const fn into_bits(self) -> u8 {
                    self as u8
                }

                #[allow(dead_code)]
                pub const fn from_bits(value: u8) -> Self {
                    match ({
                        const First: u8 = Foo::First as u8;
                        const Second: u8 = Foo::Second as u8;
                        match value {
                            First => core::result::Result::Ok(Foo::First),
                            Second => core::result::Result::Ok(Foo::Second),
                            _ => core::result::Result::Err(value),
                        }
                    }) {
                        Ok(x) => x,
                        Err(_) => panic!("invalid value for Foo"),
                    }
                }
            }

            impl core::convert::TryFrom<u8> for Foo {
                type Error = u8;

                #[allow(ambiguous_associated_items)]
                fn try_from(value: u8) -> Result<Self, u8> {
                    const First: u8 = Foo::First as u8;
                    const Second: u8 = Foo::Second as u8;
                    match value {
                        First => core::result::Result::Ok(Foo::First),
                        Second => core::result::Result::Ok(Foo::Second),
                        _ => core::result::Result::Err(value),
                    }
                }
            }
        }
    );
}
