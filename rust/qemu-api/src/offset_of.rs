// SPDX-License-Identifier: MIT

#![doc(hidden)]
//! This module provides macros that emulate the functionality of
//! `core::mem::offset_of` on older versions of Rust.
//!
//! Documentation is hidden because it only exposes macros, which
//! are exported directly from `qemu_api`.

/// This macro provides the same functionality as `core::mem::offset_of`,
/// except that only one level of field access is supported.  The declaration
/// of the struct must be wrapped with `with_offsets! { }`.
///
/// It is needed because `offset_of!` was only stabilized in Rust 1.77.
#[cfg(not(has_offset_of))]
#[macro_export]
macro_rules! offset_of {
    ($Container:ty, $field:ident) => {
        <$Container>::OFFSET_TO__.$field
    };
}

/// A wrapper for struct declarations, that allows using `offset_of!` in
/// versions of Rust prior to 1.77
#[macro_export]
macro_rules! with_offsets {
    // This method to generate field offset constants comes from:
    //
    //     https://play.rust-lang.org/?version=stable&mode=debug&edition=2018&gist=10a22a9b8393abd7b541d8fc844bc0df
    //
    // used under MIT license with permission of Yandros aka Daniel Henry-Mantilla
    (
        $(#[$struct_meta:meta])*
        $struct_vis:vis
        struct $StructName:ident {
            $(
                $(#[$field_meta:meta])*
                $field_vis:vis
                $field_name:ident : $field_ty:ty
            ),*
            $(,)?
        }
    ) => (
        #[cfg(not(has_offset_of))]
        const _: () = {
            struct StructOffsetsHelper<T>(std::marker::PhantomData<T>);
            const END_OF_PREV_FIELD: usize = 0;

            // populate StructOffsetsHelper<T> with associated consts,
            // one for each field
            $crate::with_offsets! {
                @struct $StructName
                @names [ $($field_name)* ]
                @tys [ $($field_ty ,)*]
            }

            // now turn StructOffsetsHelper<T>'s consts into a single struct,
            // applying field visibility.  This provides better error messages
            // than if offset_of! used StructOffsetsHelper::<T> directly.
            pub
            struct StructOffsets {
                $(
                    $field_vis
                    $field_name: usize,
                )*
            }
            impl $StructName {
                pub
                const OFFSET_TO__: StructOffsets = StructOffsets {
                    $(
                        $field_name: StructOffsetsHelper::<$StructName>::$field_name,
                    )*
                };
            }
        };
    );

    (
        @struct $StructName:ident
        @names []
        @tys []
    ) => ();

    (
        @struct $StructName:ident
        @names [$field_name:ident $($other_names:tt)*]
        @tys [$field_ty:ty , $($other_tys:tt)*]
    ) => (
        #[allow(non_local_definitions)]
        #[allow(clippy::modulo_one)]
        impl StructOffsetsHelper<$StructName> {
            #[allow(nonstandard_style)]
            const $field_name: usize = {
                const ALIGN: usize = std::mem::align_of::<$field_ty>();
                const TRAIL: usize = END_OF_PREV_FIELD % ALIGN;
                END_OF_PREV_FIELD + (if TRAIL == 0 { 0usize } else { ALIGN - TRAIL })
            };
        }
        const _: () = {
            const END_OF_PREV_FIELD: usize =
                StructOffsetsHelper::<$StructName>::$field_name +
                std::mem::size_of::<$field_ty>()
            ;
            $crate::with_offsets! {
                @struct $StructName
                @names [$($other_names)*]
                @tys [$($other_tys)*]
            }
        };
    );
}

#[cfg(test)]
mod tests {
    use crate::offset_of;

    #[repr(C)]
    struct Foo {
        a: u16,
        b: u32,
        c: u64,
        d: u16,
    }

    #[repr(C)]
    struct Bar {
        pub a: u16,
        pub b: u64,
        c: Foo,
        d: u64,
    }

    crate::with_offsets! {
        #[repr(C)]
        struct Bar {
            pub a: u16,
            pub b: u64,
            c: Foo,
            d: u64,
        }
    }

    #[repr(C)]
    pub struct Baz {
        b: u32,
        a: u8,
    }
    crate::with_offsets! {
        #[repr(C)]
        pub struct Baz {
            b: u32,
            a: u8,
        }
    }

    #[test]
    fn test_offset_of() {
        const OFFSET_TO_C: usize = offset_of!(Bar, c);

        assert_eq!(offset_of!(Bar, a), 0);
        assert_eq!(offset_of!(Bar, b), 8);
        assert_eq!(OFFSET_TO_C, 16);
        assert_eq!(offset_of!(Bar, d), 40);

        assert_eq!(offset_of!(Baz, b), 0);
        assert_eq!(offset_of!(Baz, a), 4);
    }
}
