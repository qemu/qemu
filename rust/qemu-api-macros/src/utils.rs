// Procedural macro utilities.
// Author(s): Paolo Bonzini <pbonzini@redhat.com>
// SPDX-License-Identifier: GPL-2.0-or-later

use proc_macro2::Span;
use quote::quote_spanned;

pub enum MacroError {
    Message(String, Span),
    ParseError(syn::Error),
}

impl From<syn::Error> for MacroError {
    fn from(err: syn::Error) -> Self {
        MacroError::ParseError(err)
    }
}

impl From<MacroError> for proc_macro2::TokenStream {
    fn from(err: MacroError) -> Self {
        match err {
            MacroError::Message(msg, span) => quote_spanned! { span => compile_error!(#msg); },
            MacroError::ParseError(err) => err.into_compile_error(),
        }
    }
}
