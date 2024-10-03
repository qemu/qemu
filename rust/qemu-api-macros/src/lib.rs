// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use proc_macro::TokenStream;
use quote::{format_ident, quote};
use syn::{parse_macro_input, DeriveInput};

#[proc_macro_derive(Object)]
pub fn derive_object(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);

    let name = input.ident;
    let module_static = format_ident!("__{}_LOAD_MODULE", name);

    let expanded = quote! {
        #[allow(non_upper_case_globals)]
        #[used]
        #[cfg_attr(target_os = "linux", link_section = ".ctors")]
        #[cfg_attr(target_os = "macos", link_section = "__DATA,__mod_init_func")]
        #[cfg_attr(target_os = "windows", link_section = ".CRT$XCU")]
        pub static #module_static: extern "C" fn() = {
            extern "C" fn __register() {
                unsafe {
                    ::qemu_api::bindings::type_register_static(&<#name as ::qemu_api::definitions::ObjectImpl>::TYPE_INFO);
                }
            }

            extern "C" fn __load() {
                unsafe {
                    ::qemu_api::bindings::register_module_init(
                        Some(__register),
                        ::qemu_api::bindings::module_init_type::MODULE_INIT_QOM
                    );
                }
            }

            __load
        };
    };

    TokenStream::from(expanded)
}
