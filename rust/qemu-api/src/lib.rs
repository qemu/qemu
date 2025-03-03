// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

#![cfg_attr(not(MESON), doc = include_str!("../README.md"))]
#![deny(clippy::missing_const_for_fn)]

#[rustfmt::skip]
pub mod bindings;

// preserve one-item-per-"use" syntax, it is clearer
// for prelude-like modules
#[rustfmt::skip]
pub mod prelude;

pub mod assertions;
pub mod bitops;
pub mod c_str;
pub mod callbacks;
pub mod cell;
pub mod chardev;
pub mod errno;
pub mod irq;
pub mod memory;
pub mod module;
pub mod offset_of;
pub mod qdev;
pub mod qom;
pub mod sysbus;
pub mod timer;
pub mod vmstate;
pub mod zeroable;

use std::{
    alloc::{GlobalAlloc, Layout},
    os::raw::c_void,
};

#[cfg(HAVE_GLIB_WITH_ALIGNED_ALLOC)]
extern "C" {
    fn g_aligned_alloc0(
        n_blocks: bindings::gsize,
        n_block_bytes: bindings::gsize,
        alignment: bindings::gsize,
    ) -> bindings::gpointer;
    fn g_aligned_free(mem: bindings::gpointer);
}

#[cfg(not(HAVE_GLIB_WITH_ALIGNED_ALLOC))]
extern "C" {
    fn qemu_memalign(alignment: usize, size: usize) -> *mut c_void;
    fn qemu_vfree(ptr: *mut c_void);
}

extern "C" {
    fn g_malloc0(n_bytes: bindings::gsize) -> bindings::gpointer;
    fn g_free(mem: bindings::gpointer);
}

/// An allocator that uses the same allocator as QEMU in C.
///
/// It is enabled by default with the `allocator` feature.
///
/// To set it up manually as a global allocator in your crate:
///
/// ```ignore
/// use qemu_api::QemuAllocator;
///
/// #[global_allocator]
/// static GLOBAL: QemuAllocator = QemuAllocator::new();
/// ```
#[derive(Clone, Copy, Debug)]
#[repr(C)]
pub struct QemuAllocator {
    _unused: [u8; 0],
}

#[cfg_attr(all(feature = "allocator", not(test)), global_allocator)]
pub static GLOBAL: QemuAllocator = QemuAllocator::new();

impl QemuAllocator {
    // From the glibc documentation, on GNU systems, malloc guarantees 16-byte
    // alignment on 64-bit systems and 8-byte alignment on 32-bit systems. See
    // https://www.gnu.org/software/libc/manual/html_node/Malloc-Examples.html.
    // This alignment guarantee also applies to Windows and Android. On Darwin
    // and OpenBSD, the alignment is 16 bytes on both 64-bit and 32-bit systems.
    #[cfg(all(
        target_pointer_width = "32",
        not(any(target_os = "macos", target_os = "openbsd"))
    ))]
    pub const DEFAULT_ALIGNMENT_BYTES: Option<usize> = Some(8);
    #[cfg(all(
        target_pointer_width = "64",
        not(any(target_os = "macos", target_os = "openbsd"))
    ))]
    pub const DEFAULT_ALIGNMENT_BYTES: Option<usize> = Some(16);
    #[cfg(all(
        any(target_pointer_width = "32", target_pointer_width = "64"),
        any(target_os = "macos", target_os = "openbsd")
    ))]
    pub const DEFAULT_ALIGNMENT_BYTES: Option<usize> = Some(16);
    #[cfg(not(any(target_pointer_width = "32", target_pointer_width = "64")))]
    pub const DEFAULT_ALIGNMENT_BYTES: Option<usize> = None;

    pub const fn new() -> Self {
        Self { _unused: [] }
    }
}

impl Default for QemuAllocator {
    fn default() -> Self {
        Self::new()
    }
}

// Sanity check.
const _: [(); 8] = [(); ::core::mem::size_of::<*mut c_void>()];

unsafe impl GlobalAlloc for QemuAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if matches!(Self::DEFAULT_ALIGNMENT_BYTES, Some(default) if default.checked_rem(layout.align()) == Some(0))
        {
            // SAFETY: g_malloc0() is safe to call.
            unsafe { g_malloc0(layout.size().try_into().unwrap()).cast::<u8>() }
        } else {
            #[cfg(HAVE_GLIB_WITH_ALIGNED_ALLOC)]
            {
                // SAFETY: g_aligned_alloc0() is safe to call.
                unsafe {
                    g_aligned_alloc0(
                        layout.size().try_into().unwrap(),
                        1,
                        layout.align().try_into().unwrap(),
                    )
                    .cast::<u8>()
                }
            }
            #[cfg(not(HAVE_GLIB_WITH_ALIGNED_ALLOC))]
            {
                // SAFETY: qemu_memalign() is safe to call.
                unsafe { qemu_memalign(layout.align(), layout.size()).cast::<u8>() }
            }
        }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        if matches!(Self::DEFAULT_ALIGNMENT_BYTES, Some(default) if default.checked_rem(layout.align()) == Some(0))
        {
            // SAFETY: `ptr` must have been allocated by Self::alloc thus a valid
            // glib-allocated pointer, so `g_free`ing is safe.
            unsafe { g_free(ptr.cast::<_>()) }
        } else {
            #[cfg(HAVE_GLIB_WITH_ALIGNED_ALLOC)]
            {
                // SAFETY: `ptr` must have been allocated by Self::alloc thus a valid aligned
                // glib-allocated pointer, so `g_aligned_free`ing is safe.
                unsafe { g_aligned_free(ptr.cast::<_>()) }
            }
            #[cfg(not(HAVE_GLIB_WITH_ALIGNED_ALLOC))]
            {
                // SAFETY: `ptr` must have been allocated by Self::alloc thus a valid aligned
                // glib-allocated pointer, so `qemu_vfree`ing is safe.
                unsafe { qemu_vfree(ptr.cast::<_>()) }
            }
        }
    }
}

#[cfg(has_offset_of)]
pub use core::mem::offset_of;
