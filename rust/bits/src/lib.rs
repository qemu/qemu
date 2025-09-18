// SPDX-License-Identifier: MIT or Apache-2.0 or GPL-2.0-or-later

/// # Definition entry point
///
/// Define a struct with a single field of type $type.  Include public constants
/// for each element listed in braces.
///
/// The unnamed element at the end, if present, can be used to enlarge the set
/// of valid bits.  Bits that are valid but not listed are treated normally for
/// the purpose of arithmetic operations, and are printed with their hexadecimal
/// value.
///
/// The struct implements the following traits: [`BitAnd`](std::ops::BitAnd),
/// [`BitOr`](std::ops::BitOr), [`BitXor`](std::ops::BitXor),
/// [`Not`](std::ops::Not), [`Sub`](std::ops::Sub); [`Debug`](std::fmt::Debug),
/// [`Display`](std::fmt::Display), [`Binary`](std::fmt::Binary),
/// [`Octal`](std::fmt::Octal), [`LowerHex`](std::fmt::LowerHex),
/// [`UpperHex`](std::fmt::UpperHex); [`From`]`<type>`/[`Into`]`<type>` where
/// type is the type specified in the definition.
///
/// ## Example
///
/// ```
/// # use bits::bits;
/// bits! {
///     pub struct Colors(u8) {
///         BLACK = 0,
///         RED = 1,
///         GREEN = 1 << 1,
///         BLUE = 1 << 2,
///         WHITE = (1 << 0) | (1 << 1) | (1 << 2),
///     }
/// }
/// ```
///
/// ```
/// # use bits::bits;
/// # bits! { pub struct Colors(u8) { BLACK = 0, RED = 1, GREEN = 1 << 1, BLUE = 1 << 2, } }
///
/// bits! {
///     pub struct Colors8(u8) {
///         BLACK = 0,
///         RED = 1,
///         GREEN = 1 << 1,
///         BLUE = 1 << 2,
///         WHITE = (1 << 0) | (1 << 1) | (1 << 2),
///
///         _ = 255,
///     }
/// }
///
/// // The previously defined struct ignores bits not explicitly defined.
/// assert_eq!(
///     Colors::from(255).into_bits(),
///     (Colors::RED | Colors::GREEN | Colors::BLUE).into_bits()
/// );
///
/// // Adding "_ = 255" makes it retain other bits as well.
/// assert_eq!(Colors8::from(255).into_bits(), 255);
///
/// // all() does not include the additional bits, valid_bits() does
/// assert_eq!(Colors8::all().into_bits(), Colors::all().into_bits());
/// assert_eq!(Colors8::valid_bits().into_bits(), 255);
/// ```
///
/// # Evaluation entry point
///
/// Return a constant corresponding to the boolean expression `$expr`.
/// Identifiers in the expression correspond to values defined for the
/// type `$type`.  Supported operators are `!` (unary), `-`, `&`, `^`, `|`.
///
/// ## Examples
///
/// ```
/// # use bits::bits;
/// bits! {
///     pub struct Colors(u8) {
///         BLACK = 0,
///         RED = 1,
///         GREEN = 1 << 1,
///         BLUE = 1 << 2,
///         // same as "WHITE = 7",
///         WHITE = bits!(Self as u8: RED | GREEN | BLUE),
///     }
/// }
///
/// let rgb = bits! { Colors: RED | GREEN | BLUE };
/// assert_eq!(rgb, Colors::WHITE);
/// ```
#[macro_export]
macro_rules! bits {
    {
        $(#[$struct_meta:meta])*
        $struct_vis:vis struct $struct_name:ident($field_vis:vis $type:ty) {
            $($(#[$const_meta:meta])* $const:ident = $val:expr),+
            $(,_ = $mask:expr)?
            $(,)?
        }
    } => {
        $(#[$struct_meta])*
        #[derive(Clone, Copy, PartialEq, Eq)]
        #[repr(transparent)]
        $struct_vis struct $struct_name($field_vis $type);

        impl $struct_name {
            $( #[allow(dead_code)] $(#[$const_meta])*
                pub const $const: $struct_name = $struct_name($val); )+

            #[doc(hidden)]
            const VALID__: $type = $( Self::$const.0 )|+ $(|$mask)?;

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn empty() -> Self {
                Self(0)
            }

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn all() -> Self {
                Self($( Self::$const.0 )|+)
            }

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn valid_bits() -> Self {
                Self(Self::VALID__)
            }

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn valid(val: $type) -> bool {
                (val & !Self::VALID__) == 0
            }

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn any_set(self, mask: Self) -> bool {
                (self.0 & mask.0) != 0
            }

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn all_set(self, mask: Self) -> bool {
                (self.0 & mask.0) == mask.0
            }

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn none_set(self, mask: Self) -> bool {
                (self.0 & mask.0) == 0
            }

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn from_bits(value: $type) -> Self {
                $struct_name(value)
            }

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn into_bits(self) -> $type {
                self.0
            }

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn set(&mut self, rhs: Self) {
                self.0 |= rhs.0;
            }

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn clear(&mut self, rhs: Self) {
                self.0 &= !rhs.0;
            }

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn toggle(&mut self, rhs: Self) {
                self.0 ^= rhs.0;
            }

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn intersection(self, rhs: Self) -> Self {
                $struct_name(self.0 & rhs.0)
            }

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn difference(self, rhs: Self) -> Self {
                $struct_name(self.0 & !rhs.0)
            }

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn symmetric_difference(self, rhs: Self) -> Self {
                $struct_name(self.0 ^ rhs.0)
            }

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn union(self, rhs: Self) -> Self {
                $struct_name(self.0 | rhs.0)
            }

            #[allow(dead_code)]
            #[inline(always)]
            pub const fn invert(self) -> Self {
                $struct_name(self.0 ^ Self::VALID__)
            }
        }

        impl ::std::fmt::Binary for $struct_name {
            fn fmt(&self, f: &mut ::std::fmt::Formatter<'_>) -> ::std::fmt::Result {
                // If no width, use the highest valid bit
                let width = f.width().unwrap_or((Self::VALID__.ilog2() + 1) as usize);
                write!(f, "{:0>width$.precision$b}", self.0,
                       width = width,
                       precision = f.precision().unwrap_or(width))
            }
        }

        impl ::std::fmt::LowerHex for $struct_name {
            fn fmt(&self, f: &mut ::std::fmt::Formatter<'_>) -> ::std::fmt::Result {
                <$type as ::std::fmt::LowerHex>::fmt(&self.0, f)
            }
        }

        impl ::std::fmt::Octal for $struct_name {
            fn fmt(&self, f: &mut ::std::fmt::Formatter<'_>) -> ::std::fmt::Result {
                <$type as ::std::fmt::Octal>::fmt(&self.0, f)
            }
        }

        impl ::std::fmt::UpperHex for $struct_name {
            fn fmt(&self, f: &mut ::std::fmt::Formatter<'_>) -> ::std::fmt::Result {
                <$type as ::std::fmt::UpperHex>::fmt(&self.0, f)
            }
        }

        impl ::std::fmt::Debug for $struct_name {
            fn fmt(&self, f: &mut ::std::fmt::Formatter<'_>) -> ::std::fmt::Result {
                write!(f, "{}({})", stringify!($struct_name), self)
            }
        }

        impl ::std::fmt::Display for $struct_name {
            fn fmt(&self, f: &mut ::std::fmt::Formatter<'_>) -> ::std::fmt::Result {
                use ::std::fmt::Display;
                let mut first = true;
                let mut left = self.0;
                $(if Self::$const.0.is_power_of_two() && (self & Self::$const).0 != 0 {
                    if first { first = false } else { Display::fmt(&'|', f)?; }
                    Display::fmt(stringify!($const), f)?;
                    left -= Self::$const.0;
                })+
                if first {
                    Display::fmt(&'0', f)
                } else if left != 0 {
                    write!(f, "|{left:#x}")
                } else {
                    Ok(())
                }
            }
        }

        impl ::std::cmp::PartialEq<$type> for $struct_name {
            fn eq(&self, rhs: &$type) -> bool {
                self.0 == *rhs
            }
        }

        impl ::std::ops::BitAnd<$struct_name> for &$struct_name {
            type Output = $struct_name;
            fn bitand(self, rhs: $struct_name) -> Self::Output {
                $struct_name(self.0 & rhs.0)
            }
        }

        impl ::std::ops::BitAndAssign<$struct_name> for $struct_name {
            fn bitand_assign(&mut self, rhs: $struct_name) {
                self.0 = self.0 & rhs.0
            }
        }

        impl ::std::ops::BitXor<$struct_name> for &$struct_name {
            type Output = $struct_name;
            fn bitxor(self, rhs: $struct_name) -> Self::Output {
                $struct_name(self.0 ^ rhs.0)
            }
        }

        impl ::std::ops::BitXorAssign<$struct_name> for $struct_name {
            fn bitxor_assign(&mut self, rhs: $struct_name) {
                self.0 = self.0 ^ rhs.0
            }
        }

        impl ::std::ops::BitOr<$struct_name> for &$struct_name {
            type Output = $struct_name;
            fn bitor(self, rhs: $struct_name) -> Self::Output {
                $struct_name(self.0 | rhs.0)
            }
        }

        impl ::std::ops::BitOrAssign<$struct_name> for $struct_name {
            fn bitor_assign(&mut self, rhs: $struct_name) {
                self.0 = self.0 | rhs.0
            }
        }

        impl ::std::ops::Sub<$struct_name> for &$struct_name {
            type Output = $struct_name;
            fn sub(self, rhs: $struct_name) -> Self::Output {
                $struct_name(self.0 & !rhs.0)
            }
        }

        impl ::std::ops::SubAssign<$struct_name> for $struct_name {
            fn sub_assign(&mut self, rhs: $struct_name) {
                self.0 = self.0 - rhs.0
            }
        }

        impl ::std::ops::Not for &$struct_name {
            type Output = $struct_name;
            fn not(self) -> Self::Output {
                $struct_name(self.0 ^ $struct_name::VALID__)
            }
        }

        impl ::std::ops::BitAnd<$struct_name> for $struct_name {
            type Output = Self;
            fn bitand(self, rhs: Self) -> Self::Output {
                $struct_name(self.0 & rhs.0)
            }
        }

        impl ::std::ops::BitXor<$struct_name> for $struct_name {
            type Output = Self;
            fn bitxor(self, rhs: Self) -> Self::Output {
                $struct_name(self.0 ^ rhs.0)
            }
        }

        impl ::std::ops::BitOr<$struct_name> for $struct_name {
            type Output = Self;
            fn bitor(self, rhs: Self) -> Self::Output {
                $struct_name(self.0 | rhs.0)
            }
        }

        impl ::std::ops::Sub<$struct_name> for $struct_name {
            type Output = Self;
            fn sub(self, rhs: Self) -> Self::Output {
                $struct_name(self.0 & !rhs.0)
            }
        }

        impl ::std::ops::Not for $struct_name {
            type Output = Self;
            fn not(self) -> Self::Output {
                $struct_name(self.0 ^ Self::VALID__)
            }
        }

        impl From<$struct_name> for $type {
            fn from(x: $struct_name) -> $type {
                x.0
            }
        }

        impl From<$type> for $struct_name {
            fn from(x: $type) -> Self {
                $struct_name(x & Self::VALID__)
            }
        }
    };

    { $type:ty: $expr:expr } => {
        $crate::bits_const_internal! { $type @ ($expr) }
    };

    { $type:ty as $int_type:ty: $expr:expr } => {
        ($crate::bits_const_internal! { $type @ ($expr) }.into_bits()) as $int_type
    };
}

#[doc(hidden)]
pub use qemu_macros::bits_const_internal;

#[cfg(test)]
mod test {
    bits! {
        pub struct InterruptMask(u32) {
            OE = 1 << 10,
            BE = 1 << 9,
            PE = 1 << 8,
            FE = 1 << 7,
            RT = 1 << 6,
            TX = 1 << 5,
            RX = 1 << 4,
            DSR = 1 << 3,
            DCD = 1 << 2,
            CTS = 1 << 1,
            RI = 1 << 0,

            E = bits!(Self as u32: OE | BE | PE | FE),
            MS = bits!(Self as u32: RI | DSR | DCD | CTS),
        }
    }

    #[test]
    pub fn test_not() {
        assert_eq!(
            !InterruptMask::from(InterruptMask::RT.0),
            InterruptMask::E | InterruptMask::MS | InterruptMask::TX | InterruptMask::RX
        );
    }

    #[test]
    pub fn test_and() {
        assert_eq!(
            InterruptMask::from(0),
            InterruptMask::MS & InterruptMask::OE
        )
    }

    #[test]
    pub fn test_or() {
        assert_eq!(
            InterruptMask::E,
            InterruptMask::OE | InterruptMask::BE | InterruptMask::PE | InterruptMask::FE
        );
    }

    #[test]
    pub fn test_xor() {
        assert_eq!(
            InterruptMask::E ^ InterruptMask::BE,
            InterruptMask::OE | InterruptMask::PE | InterruptMask::FE
        );
    }
}
