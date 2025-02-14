// Copyright (C) 2024 Intel Corporation.
// Author(s): Zhao Liu <zhai1.liu@intel.com>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::os::raw::{c_int, c_void};

use crate::{
    bindings::{self, qemu_clock_get_ns, timer_del, timer_init_full, timer_mod, QEMUClockType},
    callbacks::FnCall,
};

pub type Timer = bindings::QEMUTimer;
pub type TimerListGroup = bindings::QEMUTimerListGroup;

impl Timer {
    pub const MS: u32 = bindings::SCALE_MS;
    pub const US: u32 = bindings::SCALE_US;
    pub const NS: u32 = bindings::SCALE_NS;

    pub fn new() -> Self {
        Default::default()
    }

    const fn as_mut_ptr(&self) -> *mut Self {
        self as *const Timer as *mut _
    }

    pub fn init_full<'timer, 'opaque: 'timer, T, F>(
        &'timer mut self,
        timer_list_group: Option<&TimerListGroup>,
        clk_type: ClockType,
        scale: u32,
        attributes: u32,
        _cb: F,
        opaque: &'opaque T,
    ) where
        F: for<'a> FnCall<(&'a T,)>,
    {
        let _: () = F::ASSERT_IS_SOME;

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
                self,
                if let Some(g) = timer_list_group {
                    g as *const TimerListGroup as *mut _
                } else {
                    ::core::ptr::null_mut()
                },
                clk_type.id,
                scale as c_int,
                attributes as c_int,
                Some(timer_cb),
                (opaque as *const T).cast::<c_void>() as *mut c_void,
            )
        }
    }

    pub fn modify(&self, expire_time: u64) {
        unsafe { timer_mod(self.as_mut_ptr(), expire_time as i64) }
    }

    pub fn delete(&self) {
        unsafe { timer_del(self.as_mut_ptr()) }
    }
}

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
