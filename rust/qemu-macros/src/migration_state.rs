use std::borrow::Cow;

use proc_macro2::TokenStream;
use quote::{format_ident, quote, ToTokens};
use syn::{spanned::Spanned, DeriveInput, Error, Field, Ident, Result, Type};

use crate::get_fields;

#[derive(Debug, Default)]
enum ConversionMode {
    #[default]
    None,
    Omit,
    Into(Type),
    TryInto(Type),
    ToMigrationState,
}

impl ConversionMode {
    fn target_type(&self, original_type: &Type) -> TokenStream {
        match self {
            ConversionMode::Into(ty) | ConversionMode::TryInto(ty) => ty.to_token_stream(),
            ConversionMode::ToMigrationState => {
                quote! { <#original_type as ToMigrationState>::Migrated }
            }
            _ => original_type.to_token_stream(),
        }
    }
}

#[derive(Debug, Default)]
struct ContainerAttrs {
    rename: Option<Ident>,
}

impl ContainerAttrs {
    fn parse_from(&mut self, attrs: &[syn::Attribute]) -> Result<()> {
        use attrs::{set, with, Attrs};
        Attrs::new()
            .once("rename", with::eq(set::parse(&mut self.rename)))
            .parse_attrs("migration_state", attrs)?;
        Ok(())
    }

    fn parse(attrs: &[syn::Attribute]) -> Result<Self> {
        let mut container_attrs = Self::default();
        container_attrs.parse_from(attrs)?;
        Ok(container_attrs)
    }
}

#[derive(Debug, Default)]
struct FieldAttrs {
    conversion: ConversionMode,
    clone: bool,
}

impl FieldAttrs {
    fn parse_from(&mut self, attrs: &[syn::Attribute]) -> Result<()> {
        let mut omit_flag = false;
        let mut into_type: Option<Type> = None;
        let mut try_into_type: Option<Type> = None;

        use attrs::{set, with, Attrs};
        Attrs::new()
            .once("omit", set::flag(&mut omit_flag))
            .once("into", with::paren(set::parse(&mut into_type)))
            .once("try_into", with::paren(set::parse(&mut try_into_type)))
            .once("clone", set::flag(&mut self.clone))
            .parse_attrs("migration_state", attrs)?;

        self.conversion = match (omit_flag, into_type, try_into_type, self.clone) {
            // Valid combinations of attributes first...
            (true, None, None, false) => ConversionMode::Omit,
            (false, Some(ty), None, _) => ConversionMode::Into(ty),
            (false, None, Some(ty), _) => ConversionMode::TryInto(ty),
            (false, None, None, true) => ConversionMode::None, // clone without conversion
            (false, None, None, false) => ConversionMode::ToMigrationState, // default behavior

            // ... then the error cases
            (true, _, _, _) => {
                return Err(Error::new(
                    attrs[0].span(),
                    "ToMigrationState: omit cannot be used with other attributes",
                ));
            }
            (_, Some(_), Some(_), _) => {
                return Err(Error::new(
                    attrs[0].span(),
                    "ToMigrationState: into and try_into attributes cannot be used together",
                ));
            }
        };

        Ok(())
    }

    fn parse(attrs: &[syn::Attribute]) -> Result<Self> {
        let mut field_attrs = Self::default();
        field_attrs.parse_from(attrs)?;
        Ok(field_attrs)
    }
}

#[derive(Debug)]
struct MigrationStateField {
    name: Ident,
    original_type: Type,
    attrs: FieldAttrs,
}

impl MigrationStateField {
    fn maybe_clone(&self, mut value: TokenStream) -> TokenStream {
        if self.attrs.clone {
            value = quote! { #value.clone() };
        }
        value
    }

    fn generate_migration_state_field(&self) -> TokenStream {
        let name = &self.name;
        let field_type = self.attrs.conversion.target_type(&self.original_type);

        quote! {
            pub #name: #field_type,
        }
    }

    fn generate_snapshot_field(&self) -> TokenStream {
        let name = &self.name;
        let value = self.maybe_clone(quote! { self.#name });

        match &self.attrs.conversion {
            ConversionMode::Omit => {
                unreachable!("Omitted fields are filtered out during processing")
            }
            ConversionMode::None => quote! {
                target.#name = #value;
            },
            ConversionMode::Into(_) => quote! {
                target.#name = #value.into();
            },
            ConversionMode::TryInto(_) => quote! {
                target.#name = #value.try_into().map_err(|_| migration::InvalidError)?;
            },
            ConversionMode::ToMigrationState => quote! {
                self.#name.snapshot_migration_state(&mut target.#name)?;
            },
        }
    }

    fn generate_restore_field(&self) -> TokenStream {
        let name = &self.name;

        match &self.attrs.conversion {
            ConversionMode::Omit => {
                unreachable!("Omitted fields are filtered out during processing")
            }
            ConversionMode::None => quote! {
                self.#name = #name;
            },
            ConversionMode::Into(_) => quote! {
                self.#name = #name.into();
            },
            ConversionMode::TryInto(_) => quote! {
                self.#name = #name.try_into().map_err(|_| migration::InvalidError)?;
            },
            ConversionMode::ToMigrationState => quote! {
                self.#name.restore_migrated_state_mut(#name, _version_id)?;
            },
        }
    }
}

#[derive(Debug)]
pub struct MigrationStateDerive {
    input: DeriveInput,
    fields: Vec<MigrationStateField>,
    container_attrs: ContainerAttrs,
}

impl MigrationStateDerive {
    fn parse(input: DeriveInput) -> Result<Self> {
        let container_attrs = ContainerAttrs::parse(&input.attrs)?;
        let fields = get_fields(&input, "ToMigrationState")?;
        let fields = Self::process_fields(fields)?;

        Ok(Self {
            input,
            fields,
            container_attrs,
        })
    }

    fn process_fields(
        fields: &syn::punctuated::Punctuated<Field, syn::token::Comma>,
    ) -> Result<Vec<MigrationStateField>> {
        let processed = fields
            .iter()
            .map(|field| {
                let attrs = FieldAttrs::parse(&field.attrs)?;
                Ok((field, attrs))
            })
            .collect::<Result<Vec<_>>>()?
            .into_iter()
            .filter(|(_, attrs)| !matches!(attrs.conversion, ConversionMode::Omit))
            .map(|(field, attrs)| MigrationStateField {
                name: field.ident.as_ref().unwrap().clone(),
                original_type: field.ty.clone(),
                attrs,
            })
            .collect();

        Ok(processed)
    }

    fn migration_state_name(&self) -> Cow<'_, Ident> {
        match &self.container_attrs.rename {
            Some(rename) => Cow::Borrowed(rename),
            None => Cow::Owned(format_ident!("{}Migration", &self.input.ident)),
        }
    }

    fn generate_migration_state_struct(&self) -> TokenStream {
        let name = self.migration_state_name();
        let fields = self
            .fields
            .iter()
            .map(MigrationStateField::generate_migration_state_field);

        quote! {
            #[derive(Default)]
            pub struct #name {
                #(#fields)*
            }
        }
    }

    fn generate_snapshot_migration_state(&self) -> TokenStream {
        let fields = self
            .fields
            .iter()
            .map(MigrationStateField::generate_snapshot_field);

        quote! {
            fn snapshot_migration_state(&self, target: &mut Self::Migrated) -> Result<(), migration::InvalidError> {
                #(#fields)*
                Ok(())
            }
        }
    }

    fn generate_restore_migrated_state(&self) -> TokenStream {
        let names: Vec<_> = self.fields.iter().map(|f| &f.name).collect();
        let fields = self
            .fields
            .iter()
            .map(MigrationStateField::generate_restore_field);

        // version_id could be used or not depending on conversion attributes
        quote! {
            #[allow(clippy::used_underscore_binding)]
            fn restore_migrated_state_mut(&mut self, source: Self::Migrated, _version_id: u8) -> Result<(), migration::InvalidError> {
                let Self::Migrated { #(#names),* } = source;
                #(#fields)*
                Ok(())
            }
        }
    }

    fn generate(&self) -> TokenStream {
        let struct_name = &self.input.ident;
        let generics = &self.input.generics;

        let (impl_generics, ty_generics, where_clause) = generics.split_for_impl();
        let name = self.migration_state_name();
        let migration_state_struct = self.generate_migration_state_struct();
        let snapshot_impl = self.generate_snapshot_migration_state();
        let restore_impl = self.generate_restore_migrated_state();

        quote! {
            #migration_state_struct

            impl #impl_generics ToMigrationState for #struct_name #ty_generics #where_clause {
                type Migrated = #name;

                #snapshot_impl

                #restore_impl
            }
        }
    }

    pub fn expand(input: DeriveInput) -> Result<TokenStream> {
        let tokens = Self::parse(input)?.generate();
        Ok(tokens)
    }
}
