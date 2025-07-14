// Copyright 2025, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use quote::quote;

use super::*;

macro_rules! derive_compile_fail {
    ($derive_fn:ident, $input:expr, $error_msg:expr) => {{
        let input: proc_macro2::TokenStream = $input;
        let error_msg: &str = $error_msg;
        let derive_fn: fn(input: syn::DeriveInput) -> Result<proc_macro2::TokenStream, syn::Error> =
            $derive_fn;

        let input: syn::DeriveInput = syn::parse2(input).unwrap();
        let result = derive_fn(input);
        let err = result.unwrap_err().into_compile_error();
        assert_eq!(
            err.to_string(),
            quote! { ::core::compile_error! { #error_msg } }.to_string()
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
            ::qemu_api::assert_field_type!(
                Foo,
                _unused,
                ::qemu_api::qom::ParentField<<Foo as ::qemu_api::qom::ObjectImpl>::ParentType>
            );
            ::qemu_api::module_init! {
                MODULE_INIT_QOM => unsafe {
                    ::qemu_api::bindings::type_register_static(&<Foo as ::qemu_api::qom::ObjectImpl>::TYPE_INFO);
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
