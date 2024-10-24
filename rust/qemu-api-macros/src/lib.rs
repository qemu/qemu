// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use proc_macro::TokenStream;
use proc_macro2::Span;
use quote::{quote, quote_spanned};
use syn::{
    parse_macro_input, parse_quote, punctuated::Punctuated, token::Comma, Data, DeriveInput, Field,
    Fields, Ident, Type, Visibility,
};

struct CompileError(String, Span);

impl From<CompileError> for proc_macro2::TokenStream {
    fn from(err: CompileError) -> Self {
        let CompileError(msg, span) = err;
        quote_spanned! { span => compile_error!(#msg); }
    }
}

fn is_c_repr(input: &DeriveInput, msg: &str) -> Result<(), CompileError> {
    let expected = parse_quote! { #[repr(C)] };

    if input.attrs.iter().any(|attr| attr == &expected) {
        Ok(())
    } else {
        Err(CompileError(
            format!("#[repr(C)] required for {}", msg),
            input.ident.span(),
        ))
    }
}

#[proc_macro_derive(Object)]
pub fn derive_object(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let name = input.ident;

    let expanded = quote! {
        ::qemu_api::module_init! {
            MODULE_INIT_QOM => unsafe {
                ::qemu_api::bindings::type_register_static(&<#name as ::qemu_api::definitions::ObjectImpl>::TYPE_INFO);
            }
        }
    };

    TokenStream::from(expanded)
}

fn get_fields(input: &DeriveInput) -> Result<&Punctuated<Field, Comma>, CompileError> {
    if let Data::Struct(s) = &input.data {
        if let Fields::Named(fs) = &s.fields {
            Ok(&fs.named)
        } else {
            Err(CompileError(
                "Cannot generate offsets for unnamed fields.".to_string(),
                input.ident.span(),
            ))
        }
    } else {
        Err(CompileError(
            "Cannot generate offsets for union or enum.".to_string(),
            input.ident.span(),
        ))
    }
}

#[rustfmt::skip::macros(quote)]
fn derive_offsets_or_error(input: DeriveInput) -> Result<proc_macro2::TokenStream, CompileError> {
    is_c_repr(&input, "#[derive(offsets)]")?;

    let name = &input.ident;
    let fields = get_fields(&input)?;
    let field_names: Vec<&Ident> = fields.iter().map(|f| f.ident.as_ref().unwrap()).collect();
    let field_types: Vec<&Type> = fields.iter().map(|f| &f.ty).collect();
    let field_vis: Vec<&Visibility> = fields.iter().map(|f| &f.vis).collect();

    Ok(quote! {
	::qemu_api::with_offsets! {
	    struct #name {
		#(#field_vis #field_names: #field_types,)*
	    }
	}
    })
}

#[proc_macro_derive(offsets)]
pub fn derive_offsets(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let expanded = derive_offsets_or_error(input).unwrap_or_else(Into::into);

    TokenStream::from(expanded)
}
