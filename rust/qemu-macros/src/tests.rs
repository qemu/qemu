// Copyright 2025, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use quote::quote;

use super::*;

macro_rules! derive_compile_fail {
    ($derive_fn:path, $input:expr, $($error_msg:expr),+ $(,)?) => {{
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
    ($derive_fn:path, $input:expr, $($expected:tt)*) => {{
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
                        bitnr : {
                            const { assert!(3 >= 0 && 3 < u32::BITS as _ , "bit number exceeds type bits range"); }
                            3 as u8
                        },
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
                        bitnr : {
                            const { assert!(3 >= 0 && 3 < u32::BITS as _ , "bit number exceeds type bits range"); }
                            3 as u8
                        },
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
                        bitnr : {
                            const { assert!(3 >= 0 && 3 < u64::BITS as _ , "bit number exceeds type bits range"); }
                            3 as u8
                        },
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

#[test]
fn test_derive_to_migration_state() {
    derive_compile_fail!(
        MigrationStateDerive::expand,
        quote! {
            struct MyStruct {
                #[migration_state(omit, clone)]
                bad: u32,
            }
        },
        "ToMigrationState: omit cannot be used with other attributes"
    );
    derive_compile_fail!(
        MigrationStateDerive::expand,
        quote! {
            struct MyStruct {
                #[migration_state(into)]
                bad: u32,
            }
        },
        "unexpected end of input, expected parentheses"
    );
    derive_compile_fail!(
        MigrationStateDerive::expand,
        quote! {
            struct MyStruct {
                #[migration_state(into(String), try_into(String))]
                bad: &'static str,
            }
        },
        "ToMigrationState: into and try_into attributes cannot be used together"
    );
    derive_compile!(
        MigrationStateDerive::expand,
        quote! {
            #[migration_state(rename = CustomMigration)]
            struct MyStruct {
                #[migration_state(omit)]
                runtime_field: u32,

                #[migration_state(clone)]
                shared_data: String,

                #[migration_state(into(Cow<'static, str>), clone)]
                converted_field: String,

                #[migration_state(try_into(i8))]
                fallible_field: u32,

                nested_field: NestedStruct,
                simple_field: u32,
            }
        },
        quote! {
            #[derive(Default)]
            pub struct CustomMigration {
                pub shared_data: String,
                pub converted_field: Cow<'static, str>,
                pub fallible_field: i8,
                pub nested_field: <NestedStruct as ToMigrationState>::Migrated,
                pub simple_field: <u32 as ToMigrationState>::Migrated,
            }
            impl ToMigrationState for MyStruct {
                type Migrated = CustomMigration;
                fn snapshot_migration_state(
                    &self,
                    target: &mut Self::Migrated
                ) -> Result<(), migration::InvalidError> {
                    target.shared_data = self.shared_data.clone();
                    target.converted_field = self.converted_field.clone().into();
                    target.fallible_field = self
                        .fallible_field
                        .try_into()
                        .map_err(|_| migration::InvalidError)?;
                    self.nested_field
                        .snapshot_migration_state(&mut target.nested_field)?;
                    self.simple_field
                        .snapshot_migration_state(&mut target.simple_field)?;
                    Ok(())
                }
                #[allow(clippy::used_underscore_binding)]
                fn restore_migrated_state_mut(
                    &mut self,
                    source: Self::Migrated,
                    _version_id: u8
                ) -> Result<(), migration::InvalidError> {
                    let Self::Migrated {
                        shared_data,
                        converted_field,
                        fallible_field,
                        nested_field,
                        simple_field
                    } = source;
                    self.shared_data = shared_data;
                    self.converted_field = converted_field.into();
                    self.fallible_field = fallible_field
                        .try_into()
                        .map_err(|_| migration::InvalidError)?;
                    self.nested_field
                        .restore_migrated_state_mut(nested_field, _version_id)?;
                    self.simple_field
                        .restore_migrated_state_mut(simple_field, _version_id)?;
                    Ok(())
                }
            }
        }
    );
}
