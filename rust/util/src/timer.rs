// Copyright (C) 2024 Intel Corporation.
// Author(s): Zhao Liu <zhao1.liu@intel.com>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::{
    ffi::{c_int, c_void},
    pin::Pin,
};

use common::{callbacks::FnCall, Opaque};

use crate::bindings::{
    self, qemu_clock_get_ns, timer_del, timer_init_full, timer_mod, QEMUClockType,
};

/// A safe wrapper around [`bindings::QEMUTimer`].
#[repr(transparent)]
#[derive(Debug, common::Wrapper)]
pub struct Timer(Opaque<bindings::QEMUTimer>);

unsafe impl Send for Timer {}
unsafe impl Sync for Timer {}

#[repr(transparent)]
#[derive(common::Wrapper)]
pub struct TimerListGroup(Opaque<bindings::QEMUTimerListGroup>);

unsafe impl Send for TimerListGroup {}
unsafe impl Sync for TimerListGroup {}

impl Timer {
    pub const MS: u32 = bindings::SCALE_MS;
    pub const US: u32 = bindings::SCALE_US;
    pub const NS: u32 = bindings::SCALE_NS;

    /// Create a `Timer` struct without initializing it.
    ///
    /// # Safety
    ///
    /// The timer must be initialized before it is armed with
    /// [`modify`](Self::modify).
    pub const unsafe fn new() -> Self {
        // SAFETY: requirements relayed to callers of Timer::new
        Self(unsafe { Opaque::zeroed() })
    }

    /// Create a new timer with the given attributes.
    pub fn init_full<'timer, 'opaque: 'timer, T, F>(
        self: Pin<&'timer mut Self>,
        timer_list_group: Option<&TimerListGroup>,
        clk_type: ClockType,
        scale: u32,
        attributes: u32,
        _cb: F,
        opaque: &'opaque T,
    ) where
        F: for<'a> FnCall<(&'a T,)>,
    {
        const { assert!(F::IS_SOME) };

        /// timer expiration callback
        unsafe extern "C" fn rust_timer_handler<T, F: for<'a> FnCall<(&'a T,)>>(
            opaque: *mut c_void,
        ) {
            // SAFETY: the opaque was passed as a reference to `T`.
            F::call((unsafe { &*(opaque.cast::<T>()) },))
        }

        let timer_cb: unsafe extern "C" fn(*mut c_void) = rust_timer_handler::<T, F>;

        // SAFETY: the opaque outlives the timer
        unsafe {
            timer_init_full(
                self.as_mut_ptr(),
                if let Some(g) = timer_list_group {
                    g as *const TimerListGroup as *mut _
                } else {
                    ::core::ptr::null_mut()
                },
                clk_type.id,
                scale as c_int,
                attributes as c_int,
                Some(timer_cb),
                (opaque as *const T).cast::<c_void>().cast_mut(),
            )
        }
    }

    pub fn modify(&self, expire_time: u64) {
        // SAFETY: the only way to obtain a Timer safely is via methods that
        // take a Pin<&mut Self>, therefore the timer is pinned
        unsafe { timer_mod(self.as_mut_ptr(), expire_time as i64) }
    }

    pub fn delete(&self) {
        // SAFETY: the only way to obtain a Timer safely is via methods that
        // take a Pin<&mut Self>, therefore the timer is pinned
        unsafe { timer_del(self.as_mut_ptr()) }
    }
}

// FIXME: use something like PinnedDrop from the pinned_init crate
impl Drop for Timer {
    fn drop(&mut self) {
        self.delete()
    }
}

pub struct ClockType {
    id: QEMUClockType,
}

impl ClockType {
    pub fn get_ns(&self) -> u64 {
        // SAFETY: cannot be created outside this module, therefore id
        // is valid
        (unsafe { qemu_clock_get_ns(self.id) }) as u64
    }
}

pub const CLOCK_VIRTUAL: ClockType = ClockType {
    id: QEMUClockType::QEMU_CLOCK_VIRTUAL,
};

pub const NANOSECONDS_PER_SECOND: u64 = 1000000000;
