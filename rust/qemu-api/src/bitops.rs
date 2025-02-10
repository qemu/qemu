// Copyright (C) 2024 Intel Corporation.
// Author(s): Zhao Liu <zhai1.liu@intel.com>
// SPDX-License-Identifier: GPL-2.0-or-later

//! This module provides bit operation extensions to integer types.
//! It is usually included via the `qemu_api` prelude.

use std::ops::{
    Add, AddAssign, BitAnd, BitAndAssign, BitOr, BitOrAssign, BitXor, BitXorAssign, Div, DivAssign,
    Mul, MulAssign, Not, Rem, RemAssign, Shl, ShlAssign, Shr, ShrAssign,
};

/// Trait for extensions to integer types
pub trait IntegerExt:
    Add<Self, Output = Self> + AddAssign<Self> +
    BitAnd<Self, Output = Self> + BitAndAssign<Self> +
    BitOr<Self, Output = Self> + BitOrAssign<Self> +
    BitXor<Self, Output = Self> + BitXorAssign<Self> +
    Copy +
    Div<Self, Output = Self> + DivAssign<Self> +
    Eq +
    Mul<Self, Output = Self> + MulAssign<Self> +
    Not<Output = Self> + Ord + PartialOrd +
    Rem<Self, Output = Self> + RemAssign<Self> +
    Shl<Self, Output = Self> + ShlAssign<Self> +
    Shl<u32, Output = Self> + ShlAssign<u32> + // add more as needed
    Shr<Self, Output = Self> + ShrAssign<Self> +
    Shr<u32, Output = Self> + ShrAssign<u32> // add more as needed
{
    const BITS: u32;
    const MAX: Self;
    const MIN: Self;
    const ONE: Self;
    const ZERO: Self;

    #[inline]
    #[must_use]
    fn bit(start: u32) -> Self
    {
        debug_assert!(start < Self::BITS);

        Self::ONE << start
    }

    #[inline]
    #[must_use]
    fn mask(start: u32, length: u32) -> Self
    {
        /* FIXME: Implement a more elegant check with error handling support? */
        debug_assert!(start < Self::BITS && length > 0 && length <= Self::BITS - start);

        (Self::MAX >> (Self::BITS - length)) << start
    }

    #[inline]
    #[must_use]
    fn deposit<U: IntegerExt>(self, start: u32, length: u32,
                          fieldval: U) -> Self
        where Self: From<U>
    {
        debug_assert!(length <= U::BITS);

        let mask = Self::mask(start, length);
        (self & !mask) | ((Self::from(fieldval) << start) & mask)
    }

    #[inline]
    #[must_use]
    fn extract(self, start: u32, length: u32) -> Self
    {
        let mask = Self::mask(start, length);
        (self & mask) >> start
    }
}

macro_rules! impl_num_ext {
    ($type:ty) => {
        impl IntegerExt for $type {
            const BITS: u32 = <$type>::BITS;
            const MAX: Self = <$type>::MAX;
            const MIN: Self = <$type>::MIN;
            const ONE: Self = 1;
            const ZERO: Self = 0;
        }
    };
}

impl_num_ext!(u8);
impl_num_ext!(u16);
impl_num_ext!(u32);
impl_num_ext!(u64);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_deposit() {
        assert_eq!(15u32.deposit(8, 8, 1u32), 256 + 15);
        assert_eq!(15u32.deposit(8, 1, 255u8), 256 + 15);
    }

    #[test]
    fn test_extract() {
        assert_eq!(15u32.extract(2, 4), 3);
    }

    #[test]
    fn test_bit() {
        assert_eq!(u8::bit(7), 128);
        assert_eq!(u32::bit(16), 0x10000);
    }

    #[test]
    fn test_mask() {
        assert_eq!(u8::mask(7, 1), 128);
        assert_eq!(u32::mask(8, 8), 0xff00);
    }
}
