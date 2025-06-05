// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use proc_macro::TokenStream;
use quote::quote;
use syn::{
    parse_macro_input, parse_quote, punctuated::Punctuated, spanned::Spanned, token::Comma, Data,
    DeriveInput, Field, Fields, FieldsUnnamed, Ident, Meta, Path, Token, Variant,
};

mod utils;
use utils::MacroError;

mod bits;
use bits::BitsConstInternal;

fn get_fields<'a>(
    input: &'a DeriveInput,
    msg: &str,
) -> Result<&'a Punctuated<Field, Comma>, MacroError> {
    let Data::Struct(ref s) = &input.data else {
        return Err(MacroError::Message(
            format!("Struct required for {msg}"),
            input.ident.span(),
        ));
    };
    let Fields::Named(ref fs) = &s.fields else {
        return Err(MacroError::Message(
            format!("Named fields required for {msg}"),
            input.ident.span(),
        ));
    };
    Ok(&fs.named)
}

fn get_unnamed_field<'a>(input: &'a DeriveInput, msg: &str) -> Result<&'a Field, MacroError> {
    let Data::Struct(ref s) = &input.data else {
        return Err(MacroError::Message(
            format!("Struct required for {msg}"),
            input.ident.span(),
        ));
    };
    let Fields::Unnamed(FieldsUnnamed { ref unnamed, .. }) = &s.fields else {
        return Err(MacroError::Message(
            format!("Tuple struct required for {msg}"),
            s.fields.span(),
        ));
    };
    if unnamed.len() != 1 {
        return Err(MacroError::Message(
            format!("A single field is required for {msg}"),
            s.fields.span(),
        ));
    }
    Ok(&unnamed[0])
}

fn is_c_repr(input: &DeriveInput, msg: &str) -> Result<(), MacroError> {
    let expected = parse_quote! { #[repr(C)] };

    if input.attrs.iter().any(|attr| attr == &expected) {
        Ok(())
    } else {
        Err(MacroError::Message(
            format!("#[repr(C)] required for {msg}"),
            input.ident.span(),
        ))
    }
}

fn is_transparent_repr(input: &DeriveInput, msg: &str) -> Result<(), MacroError> {
    let expected = parse_quote! { #[repr(transparent)] };

    if input.attrs.iter().any(|attr| attr == &expected) {
        Ok(())
    } else {
        Err(MacroError::Message(
            format!("#[repr(transparent)] required for {msg}"),
            input.ident.span(),
        ))
    }
}

fn derive_object_or_error(input: DeriveInput) -> Result<proc_macro2::TokenStream, MacroError> {
    is_c_repr(&input, "#[derive(Object)]")?;

    let name = &input.ident;
    let parent = &get_fields(&input, "#[derive(Object)]")?[0].ident;

    Ok(quote! {
        ::qemu_api::assert_field_type!(#name, #parent,
            ::qemu_api::qom::ParentField<<#name as ::qemu_api::qom::ObjectImpl>::ParentType>);

        ::qemu_api::module_init! {
            MODULE_INIT_QOM => unsafe {
                ::qemu_api::bindings::type_register_static(&<#name as ::qemu_api::qom::ObjectImpl>::TYPE_INFO);
            }
        }
    })
}

#[proc_macro_derive(Object)]
pub fn derive_object(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let expanded = derive_object_or_error(input).unwrap_or_else(Into::into);

    TokenStream::from(expanded)
}

fn derive_opaque_or_error(input: DeriveInput) -> Result<proc_macro2::TokenStream, MacroError> {
    is_transparent_repr(&input, "#[derive(Wrapper)]")?;

    let name = &input.ident;
    let field = &get_unnamed_field(&input, "#[derive(Wrapper)]")?;
    let typ = &field.ty;

    // TODO: how to add "::qemu_api"?  For now, this is only used in the
    // qemu_api crate so it's not a problem.
    Ok(quote! {
        unsafe impl crate::cell::Wrapper for #name {
            type Wrapped = <#typ as crate::cell::Wrapper>::Wrapped;
        }
        impl #name {
            pub unsafe fn from_raw<'a>(ptr: *mut <Self as crate::cell::Wrapper>::Wrapped) -> &'a Self {
                let ptr = ::std::ptr::NonNull::new(ptr).unwrap().cast::<Self>();
                unsafe { ptr.as_ref() }
            }

            pub const fn as_mut_ptr(&self) -> *mut <Self as crate::cell::Wrapper>::Wrapped {
                self.0.as_mut_ptr()
            }

            pub const fn as_ptr(&self) -> *const <Self as crate::cell::Wrapper>::Wrapped {
                self.0.as_ptr()
            }

            pub const fn as_void_ptr(&self) -> *mut ::core::ffi::c_void {
                self.0.as_void_ptr()
            }

            pub const fn raw_get(slot: *mut Self) -> *mut <Self as crate::cell::Wrapper>::Wrapped {
                slot.cast()
            }
        }
    })
}

#[proc_macro_derive(Wrapper)]
pub fn derive_opaque(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let expanded = derive_opaque_or_error(input).unwrap_or_else(Into::into);

    TokenStream::from(expanded)
}

#[allow(non_snake_case)]
fn get_repr_uN(input: &DeriveInput, msg: &str) -> Result<Path, MacroError> {
    let repr = input.attrs.iter().find(|attr| attr.path().is_ident("repr"));
    if let Some(repr) = repr {
        let nested = repr.parse_args_with(Punctuated::<Meta, Token![,]>::parse_terminated)?;
        for meta in nested {
            match meta {
                Meta::Path(path) if path.is_ident("u8") => return Ok(path),
                Meta::Path(path) if path.is_ident("u16") => return Ok(path),
                Meta::Path(path) if path.is_ident("u32") => return Ok(path),
                Meta::Path(path) if path.is_ident("u64") => return Ok(path),
                _ => {}
            }
        }
    }

    Err(MacroError::Message(
        format!("#[repr(u8/u16/u32/u64) required for {msg}"),
        input.ident.span(),
    ))
}

fn get_variants(input: &DeriveInput) -> Result<&Punctuated<Variant, Comma>, MacroError> {
    let Data::Enum(ref e) = &input.data else {
        return Err(MacroError::Message(
            "Cannot derive TryInto for union or struct.".to_string(),
            input.ident.span(),
        ));
    };
    if let Some(v) = e.variants.iter().find(|v| v.fields != Fields::Unit) {
        return Err(MacroError::Message(
            "Cannot derive TryInto for enum with non-unit variants.".to_string(),
            v.fields.span(),
        ));
    }
    Ok(&e.variants)
}

#[rustfmt::skip::macros(quote)]
fn derive_tryinto_body(
    name: &Ident,
    variants: &Punctuated<Variant, Comma>,
    repr: &Path,
) -> Result<proc_macro2::TokenStream, MacroError> {
    let discriminants: Vec<&Ident> = variants.iter().map(|f| &f.ident).collect();

    Ok(quote! {
        #(const #discriminants: #repr = #name::#discriminants as #repr;)*;
        match value {
            #(#discriminants => core::result::Result::Ok(#name::#discriminants),)*
            _ => core::result::Result::Err(value),
        }
    })
}

#[rustfmt::skip::macros(quote)]
fn derive_tryinto_or_error(input: DeriveInput) -> Result<proc_macro2::TokenStream, MacroError> {
    let repr = get_repr_uN(&input, "#[derive(TryInto)]")?;
    let name = &input.ident;
    let body = derive_tryinto_body(name, get_variants(&input)?, &repr)?;
    let errmsg = format!("invalid value for {name}");

    Ok(quote! {
        impl #name {
            #[allow(dead_code)]
            pub const fn into_bits(self) -> #repr {
                self as #repr
            }

            #[allow(dead_code)]
            pub const fn from_bits(value: #repr) -> Self {
                match ({
                    #body
                }) {
                    Ok(x) => x,
                    Err(_) => panic!(#errmsg)
                }
            }
        }
        impl core::convert::TryFrom<#repr> for #name {
            type Error = #repr;

            #[allow(ambiguous_associated_items)]
            fn try_from(value: #repr) -> Result<Self, #repr> {
                #body
            }
        }
    })
}

#[proc_macro_derive(TryInto)]
pub fn derive_tryinto(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let expanded = derive_tryinto_or_error(input).unwrap_or_else(Into::into);

    TokenStream::from(expanded)
}

#[proc_macro]
pub fn bits_const_internal(ts: TokenStream) -> TokenStream {
    let ts = proc_macro2::TokenStream::from(ts);
    let mut it = ts.into_iter();

    let expanded = BitsConstInternal::parse(&mut it).unwrap_or_else(Into::into);
    TokenStream::from(expanded)
}
