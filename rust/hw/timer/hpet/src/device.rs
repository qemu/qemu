// Copyright (C) 2024 Intel Corporation.
// Author(s): Zhao Liu <zhao1.liu@intel.com>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::{
    ffi::CStr,
    mem::MaybeUninit,
    pin::Pin,
    ptr::{addr_of_mut, null_mut, NonNull},
    slice::from_ref,
};

use bql::{BqlCell, BqlRefCell};
use common::{bitops::IntegerExt, uninit_field_mut};
use hwcore::{
    DeviceImpl, DeviceMethods, DeviceState, InterruptSource, ResetType, ResettablePhasesImpl,
    SysBusDevice, SysBusDeviceImpl, SysBusDeviceMethods,
};
use migration::{
    self, impl_vmstate_struct, vmstate_fields, vmstate_of, vmstate_subsections, vmstate_validate,
    VMStateDescription, VMStateDescriptionBuilder,
};
use qom::{prelude::*, ObjectImpl, ParentField, ParentInit};
use system::{
    bindings::{address_space_memory, address_space_stl_le, hwaddr},
    MemoryRegion, MemoryRegionOps, MemoryRegionOpsBuilder, MEMTXATTRS_UNSPECIFIED,
};
use util::{
    ensure,
    timer::{Timer, CLOCK_VIRTUAL, NANOSECONDS_PER_SECOND},
};

use crate::fw_cfg::HPETFwConfig;

/// Register space for each timer block (`HPET_BASE` is defined in hpet.h).
const HPET_REG_SPACE_LEN: u64 = 0x400; // 1024 bytes

/// Minimum recommended hardware implementation.
const HPET_MIN_TIMERS: usize = 3;
/// Maximum timers in each timer block.
const HPET_MAX_TIMERS: usize = 32;

/// Flags that HPETState.flags supports.
const HPET_FLAG_MSI_SUPPORT_SHIFT: usize = 0;

const HPET_NUM_IRQ_ROUTES: usize = 32;
const HPET_LEGACY_PIT_INT: u32 = 0; // HPET_LEGACY_RTC_INT isn't defined here.
const RTC_ISA_IRQ: usize = 8;

const HPET_CLK_PERIOD: u64 = 10; // 10 ns
const FS_PER_NS: u64 = 1000000; // 1000000 femtoseconds == 1 ns

/// Revision ID (bits 0:7). Revision 1 is implemented (refer to v1.0a spec).
const HPET_CAP_REV_ID_VALUE: u64 = 0x1;
const HPET_CAP_REV_ID_SHIFT: usize = 0;
/// Number of Timers (bits 8:12)
const HPET_CAP_NUM_TIM_SHIFT: usize = 8;
/// Counter Size (bit 13)
const HPET_CAP_COUNT_SIZE_CAP_SHIFT: usize = 13;
/// Legacy Replacement Route Capable (bit 15)
const HPET_CAP_LEG_RT_CAP_SHIFT: usize = 15;
/// Vendor ID (bits 16:31)
const HPET_CAP_VENDER_ID_VALUE: u64 = 0x8086;
const HPET_CAP_VENDER_ID_SHIFT: usize = 16;
/// Main Counter Tick Period (bits 32:63)
const HPET_CAP_CNT_CLK_PERIOD_SHIFT: usize = 32;

/// Overall Enable (bit 0)
const HPET_CFG_ENABLE_SHIFT: usize = 0;
/// Legacy Replacement Route (bit 1)
const HPET_CFG_LEG_RT_SHIFT: usize = 1;
/// Other bits are reserved.
const HPET_CFG_WRITE_MASK: u64 = 0x003;

/// bit 0, 7, and bits 16:31 are reserved.
/// bit 4, 5, 15, and bits 32:64 are read-only.
const HPET_TN_CFG_WRITE_MASK: u64 = 0x7f4e;
/// Timer N Interrupt Type (bit 1)
const HPET_TN_CFG_INT_TYPE_SHIFT: usize = 1;
/// Timer N Interrupt Enable (bit 2)
const HPET_TN_CFG_INT_ENABLE_SHIFT: usize = 2;
/// Timer N Type (Periodic enabled or not, bit 3)
const HPET_TN_CFG_PERIODIC_SHIFT: usize = 3;
/// Timer N Periodic Interrupt Capable (support Periodic or not, bit 4)
const HPET_TN_CFG_PERIODIC_CAP_SHIFT: usize = 4;
/// Timer N Size (timer size is 64-bits or 32 bits, bit 5)
const HPET_TN_CFG_SIZE_CAP_SHIFT: usize = 5;
/// Timer N Value Set (bit 6)
const HPET_TN_CFG_SETVAL_SHIFT: usize = 6;
/// Timer N 32-bit Mode (bit 8)
const HPET_TN_CFG_32BIT_SHIFT: usize = 8;
/// Timer N Interrupt Rout (bits 9:13)
const HPET_TN_CFG_INT_ROUTE_MASK: u64 = 0x3e00;
const HPET_TN_CFG_INT_ROUTE_SHIFT: usize = 9;
/// Timer N FSB Interrupt Enable (bit 14)
const HPET_TN_CFG_FSB_ENABLE_SHIFT: usize = 14;
/// Timer N FSB Interrupt Delivery (bit 15)
const HPET_TN_CFG_FSB_CAP_SHIFT: usize = 15;
/// Timer N Interrupt Routing Capability (bits 32:63)
const HPET_TN_CFG_INT_ROUTE_CAP_SHIFT: usize = 32;

#[derive(common::TryInto)]
#[repr(u64)]
#[allow(non_camel_case_types)]
/// Timer registers, masked by 0x18
enum TimerRegister {
    /// Timer N Configuration and Capability Register
    CFG = 0,
    /// Timer N Comparator Value Register
    CMP = 8,
    /// Timer N FSB Interrupt Route Register
    ROUTE = 16,
}

#[derive(common::TryInto)]
#[repr(u64)]
#[allow(non_camel_case_types)]
/// Global registers
enum GlobalRegister {
    /// General Capabilities and ID Register
    CAP = 0,
    /// General Configuration Register
    CFG = 0x10,
    /// General Interrupt Status Register
    INT_STATUS = 0x20,
    /// Main Counter Value Register
    COUNTER = 0xF0,
}

enum HPETRegister<'a> {
    /// Global register in the range from `0` to `0xff`
    Global(GlobalRegister),

    /// Register in the timer block `0x100`...`0x3ff`
    Timer(&'a BqlRefCell<HPETTimer>, TimerRegister),

    /// Invalid address
    #[allow(dead_code)]
    Unknown(hwaddr),
}

struct HPETAddrDecode<'a> {
    shift: u32,
    len: u32,
    reg: HPETRegister<'a>,
}

const fn hpet_next_wrap(cur_tick: u64) -> u64 {
    (cur_tick | 0xffffffff) + 1
}

const fn hpet_time_after(a: u64, b: u64) -> bool {
    ((b - a) as i64) < 0
}

const fn ticks_to_ns(value: u64) -> u64 {
    value * HPET_CLK_PERIOD
}

const fn ns_to_ticks(value: u64) -> u64 {
    value / HPET_CLK_PERIOD
}

// Avoid touching the bits that cannot be written.
const fn hpet_fixup_reg(new: u64, old: u64, mask: u64) -> u64 {
    (new & mask) | (old & !mask)
}

const fn activating_bit(old: u64, new: u64, shift: usize) -> bool {
    let mask: u64 = 1 << shift;
    (old & mask == 0) && (new & mask != 0)
}

const fn deactivating_bit(old: u64, new: u64, shift: usize) -> bool {
    let mask: u64 = 1 << shift;
    (old & mask != 0) && (new & mask == 0)
}

fn timer_handler(timer_cell: &BqlRefCell<HPETTimer>) {
    timer_cell.borrow_mut().callback()
}

/// HPET Timer Abstraction
#[repr(C)]
#[derive(Debug)]
pub struct HPETTimer {
    /// timer N index within the timer block (`HPETState`)
    #[doc(alias = "tn")]
    index: u8,
    qemu_timer: Timer,
    /// timer block abstraction containing this timer
    state: NonNull<HPETState>,

    // Memory-mapped, software visible timer registers
    /// Timer N Configuration and Capability Register
    config: u64,
    /// Timer N Comparator Value Register
    cmp: u64,
    /// Timer N FSB Interrupt Route Register
    fsb: u64,

    // Hidden register state
    /// comparator (extended to counter width)
    cmp64: u64,
    /// Last value written to comparator
    period: u64,
    /// timer pop will indicate wrap for one-shot 32-bit
    /// mode. Next pop will be actual timer expiration.
    wrap_flag: u8,
    /// last value armed, to avoid timer storms
    last: u64,
}

// SAFETY: Sync is not automatically derived due to the `state` field,
// which is always dereferenced to a shared reference.
unsafe impl Sync for HPETTimer {}

impl HPETTimer {
    fn new(index: u8, state: *const HPETState) -> HPETTimer {
        HPETTimer {
            index,
            // SAFETY: the HPETTimer will only be used after the timer
            // is initialized below.
            qemu_timer: unsafe { Timer::new() },
            state: NonNull::new(state.cast_mut()).unwrap(),
            config: 0,
            cmp: 0,
            fsb: 0,
            cmp64: 0,
            period: 0,
            wrap_flag: 0,
            last: 0,
        }
    }

    fn init_timer_with_cell(cell: &BqlRefCell<Self>) {
        let mut timer = cell.borrow_mut();
        // SAFETY: HPETTimer is only used as part of HPETState, which is
        // always pinned.
        let qemu_timer = unsafe { Pin::new_unchecked(&mut timer.qemu_timer) };
        qemu_timer.init_full(None, CLOCK_VIRTUAL, Timer::NS, 0, timer_handler, cell);
    }

    fn get_state(&self) -> &HPETState {
        // SAFETY:
        // the pointer is convertible to a reference
        unsafe { self.state.as_ref() }
    }

    fn is_int_active(&self) -> bool {
        self.get_state().is_timer_int_active(self.index.into())
    }

    const fn is_fsb_route_enabled(&self) -> bool {
        self.config & (1 << HPET_TN_CFG_FSB_ENABLE_SHIFT) != 0
    }

    const fn is_periodic(&self) -> bool {
        self.config & (1 << HPET_TN_CFG_PERIODIC_SHIFT) != 0
    }

    const fn is_int_enabled(&self) -> bool {
        self.config & (1 << HPET_TN_CFG_INT_ENABLE_SHIFT) != 0
    }

    const fn is_32bit_mod(&self) -> bool {
        self.config & (1 << HPET_TN_CFG_32BIT_SHIFT) != 0
    }

    const fn is_valset_enabled(&self) -> bool {
        self.config & (1 << HPET_TN_CFG_SETVAL_SHIFT) != 0
    }

    fn clear_valset(&mut self) {
        self.config &= !(1 << HPET_TN_CFG_SETVAL_SHIFT);
    }

    /// True if timer interrupt is level triggered; otherwise, edge triggered.
    const fn is_int_level_triggered(&self) -> bool {
        self.config & (1 << HPET_TN_CFG_INT_TYPE_SHIFT) != 0
    }

    /// calculate next value of the general counter that matches the
    /// target (either entirely, or the low 32-bit only depending on
    /// the timer mode).
    fn calculate_cmp64(&self, cur_tick: u64, target: u64) -> u64 {
        if self.is_32bit_mod() {
            let mut result: u64 = cur_tick.deposit(0, 32, target);
            if result < cur_tick {
                result += 0x100000000;
            }
            result
        } else {
            target
        }
    }

    const fn get_individual_route(&self) -> usize {
        ((self.config & HPET_TN_CFG_INT_ROUTE_MASK) >> HPET_TN_CFG_INT_ROUTE_SHIFT) as usize
    }

    fn get_int_route(&self) -> usize {
        if self.index <= 1 && self.get_state().is_legacy_mode() {
            // If LegacyReplacement Route bit is set, HPET specification requires
            // timer0 be routed to IRQ0 in NON-APIC or IRQ2 in the I/O APIC,
            // timer1 be routed to IRQ8 in NON-APIC or IRQ8 in the I/O APIC.
            //
            // If the LegacyReplacement Route bit is set, the individual routing
            // bits for timers 0 and 1 (APIC or FSB) will have no impact.
            //
            // FIXME: Consider I/O APIC case.
            if self.index == 0 {
                0
            } else {
                RTC_ISA_IRQ
            }
        } else {
            // (If the LegacyReplacement Route bit is set) Timer 2-n will be
            // routed as per the routing in the timer n config registers.
            // ...
            // If the LegacyReplacement Route bit is not set, the individual
            // routing bits for each of the timers are used.
            self.get_individual_route()
        }
    }

    fn set_irq(&mut self, set: bool) {
        let route = self.get_int_route();

        if set && self.is_int_enabled() && self.get_state().is_hpet_enabled() {
            if self.is_fsb_route_enabled() {
                // SAFETY:
                // the parameters are valid.
                unsafe {
                    address_space_stl_le(
                        addr_of_mut!(address_space_memory),
                        self.fsb >> 32,  // Timer N FSB int addr
                        self.fsb as u32, // Timer N FSB int value, truncate!
                        MEMTXATTRS_UNSPECIFIED,
                        null_mut(),
                    );
                }
            } else if self.is_int_level_triggered() {
                self.get_state().irqs[route].raise();
            } else {
                self.get_state().irqs[route].pulse();
            }
        } else if !self.is_fsb_route_enabled() {
            self.get_state().irqs[route].lower();
        }
    }

    fn update_irq(&mut self, set: bool) {
        // If Timer N Interrupt Enable bit is 0, "the timer will
        // still operate and generate appropriate status bits, but
        // will not cause an interrupt"
        self.get_state()
            .update_int_status(self.index.into(), set && self.is_int_level_triggered());
        self.set_irq(set);
    }

    fn arm_timer(&mut self, tick: u64) {
        let mut ns = self.get_state().get_ns(tick);

        // Clamp period to reasonable min value (1 us)
        if self.is_periodic() && ns - self.last < 1000 {
            ns = self.last + 1000;
        }

        self.last = ns;
        self.qemu_timer.modify(self.last);
    }

    fn set_timer(&mut self) {
        let cur_tick: u64 = self.get_state().get_ticks();

        self.wrap_flag = 0;
        self.cmp64 = self.calculate_cmp64(cur_tick, self.cmp);
        if self.is_32bit_mod() {
            // HPET spec says in one-shot 32-bit mode, generate an interrupt when
            // counter wraps in addition to an interrupt with comparator match.
            if !self.is_periodic() && self.cmp64 > hpet_next_wrap(cur_tick) {
                self.wrap_flag = 1;
                self.arm_timer(hpet_next_wrap(cur_tick));
                return;
            }
        }
        self.arm_timer(self.cmp64);
    }

    fn del_timer(&mut self) {
        // Just remove the timer from the timer_list without destroying
        // this timer instance.
        self.qemu_timer.delete();

        if self.is_int_active() {
            // For level-triggered interrupt, this leaves interrupt status
            // register set but lowers irq.
            self.update_irq(true);
        }
    }

    /// Configuration and Capability Register
    fn set_tn_cfg_reg(&mut self, shift: u32, len: u32, val: u64) {
        // TODO: Add trace point - trace_hpet_ram_write_tn_cfg(addr & 4)
        let old_val: u64 = self.config;
        let mut new_val: u64 = old_val.deposit(shift, len, val);
        new_val = hpet_fixup_reg(new_val, old_val, HPET_TN_CFG_WRITE_MASK);

        // Switch level-type interrupt to edge-type.
        if deactivating_bit(old_val, new_val, HPET_TN_CFG_INT_TYPE_SHIFT) {
            // Do this before changing timer.config; otherwise, if
            // HPET_TN_FSB is set, update_irq will not lower the qemu_irq.
            self.update_irq(false);
        }

        self.config = new_val;

        if activating_bit(old_val, new_val, HPET_TN_CFG_INT_ENABLE_SHIFT) && self.is_int_active() {
            self.update_irq(true);
        }

        if self.is_32bit_mod() {
            self.cmp = u64::from(self.cmp as u32); // truncate!
            self.period = u64::from(self.period as u32); // truncate!
        }

        if self.get_state().is_hpet_enabled() {
            self.set_timer();
        }
    }

    /// Comparator Value Register
    fn set_tn_cmp_reg(&mut self, shift: u32, len: u32, val: u64) {
        let mut length = len;
        let mut value = val;

        // TODO: Add trace point - trace_hpet_ram_write_tn_cmp(addr & 4)
        if self.is_32bit_mod() {
            // High 32-bits are zero, leave them untouched.
            if shift != 0 {
                // TODO: Add trace point - trace_hpet_ram_write_invalid_tn_cmp()
                return;
            }
            length = 64;
            value = u64::from(value as u32); // truncate!
        }

        if !self.is_periodic() || self.is_valset_enabled() {
            self.cmp = self.cmp.deposit(shift, length, value);
        }

        if self.is_periodic() {
            self.period = self.period.deposit(shift, length, value);
        }

        self.clear_valset();
        if self.get_state().is_hpet_enabled() {
            self.set_timer();
        }
    }

    /// FSB Interrupt Route Register
    fn set_tn_fsb_route_reg(&mut self, shift: u32, len: u32, val: u64) {
        self.fsb = self.fsb.deposit(shift, len, val);
    }

    fn reset(&mut self) {
        self.del_timer();
        self.cmp = u64::MAX; // Comparator Match Registers reset to all 1's.
        self.config = (1 << HPET_TN_CFG_PERIODIC_CAP_SHIFT) | (1 << HPET_TN_CFG_SIZE_CAP_SHIFT);
        if self.get_state().has_msi_flag() {
            self.config |= 1 << HPET_TN_CFG_FSB_CAP_SHIFT;
        }
        // advertise availability of ioapic int
        self.config |=
            (u64::from(self.get_state().int_route_cap)) << HPET_TN_CFG_INT_ROUTE_CAP_SHIFT;
        self.period = 0;
        self.wrap_flag = 0;
    }

    /// timer expiration callback
    fn callback(&mut self) {
        let period: u64 = self.period;
        let cur_tick: u64 = self.get_state().get_ticks();

        if self.is_periodic() && period != 0 {
            while hpet_time_after(cur_tick, self.cmp64) {
                self.cmp64 += period;
            }
            if self.is_32bit_mod() {
                self.cmp = u64::from(self.cmp64 as u32); // truncate!
            } else {
                self.cmp = self.cmp64;
            }
            self.arm_timer(self.cmp64);
        } else if self.wrap_flag != 0 {
            self.wrap_flag = 0;
            self.arm_timer(self.cmp64);
        }
        self.update_irq(true);
    }

    const fn read(&self, reg: TimerRegister) -> u64 {
        use TimerRegister::*;
        match reg {
            CFG => self.config, // including interrupt capabilities
            CMP => self.cmp,    // comparator register
            ROUTE => self.fsb,
        }
    }

    fn write(&mut self, reg: TimerRegister, value: u64, shift: u32, len: u32) {
        use TimerRegister::*;
        match reg {
            CFG => self.set_tn_cfg_reg(shift, len, value),
            CMP => self.set_tn_cmp_reg(shift, len, value),
            ROUTE => self.set_tn_fsb_route_reg(shift, len, value),
        }
    }
}

/// HPET Event Timer Block Abstraction
#[repr(C)]
#[derive(qom::Object, hwcore::Device)]
pub struct HPETState {
    parent_obj: ParentField<SysBusDevice>,
    iomem: MemoryRegion,

    // HPET block Registers: Memory-mapped, software visible registers
    /// General Capabilities and ID Register
    capability: BqlCell<u64>,
    ///  General Configuration Register
    config: BqlCell<u64>,
    /// General Interrupt Status Register
    #[doc(alias = "isr")]
    int_status: BqlCell<u64>,
    /// Main Counter Value Register
    #[doc(alias = "hpet_counter")]
    counter: BqlCell<u64>,

    // Internal state
    /// Capabilities that QEMU HPET supports.
    /// bit 0: MSI (or FSB) support.
    #[property(rename = "msi", bit = HPET_FLAG_MSI_SUPPORT_SHIFT, default = false)]
    flags: u32,

    /// Offset of main counter relative to qemu clock.
    hpet_offset: BqlCell<u64>,
    #[property(rename = "hpet-offset-saved", default = true)]
    hpet_offset_saved: bool,

    irqs: [InterruptSource; HPET_NUM_IRQ_ROUTES],
    rtc_irq_level: BqlCell<u32>,
    pit_enabled: InterruptSource,

    /// Interrupt Routing Capability.
    /// This field indicates to which interrupts in the I/O (x) APIC
    /// the timers' interrupt can be routed, and is encoded in the
    /// bits 32:64 of timer N's config register:
    #[doc(alias = "intcap")]
    #[property(rename = "hpet-intcap", default = 0)]
    int_route_cap: u32,

    /// HPET timer array managed by this timer block.
    #[doc(alias = "timer")]
    timers: [BqlRefCell<HPETTimer>; HPET_MAX_TIMERS],
    #[property(rename = "timers", default = HPET_MIN_TIMERS)]
    num_timers: usize,
    num_timers_save: BqlCell<u8>,

    /// Instance id (HPET timer block ID).
    hpet_id: BqlCell<usize>,
}

impl HPETState {
    const fn has_msi_flag(&self) -> bool {
        self.flags & (1 << HPET_FLAG_MSI_SUPPORT_SHIFT) != 0
    }

    fn is_legacy_mode(&self) -> bool {
        self.config.get() & (1 << HPET_CFG_LEG_RT_SHIFT) != 0
    }

    fn is_hpet_enabled(&self) -> bool {
        self.config.get() & (1 << HPET_CFG_ENABLE_SHIFT) != 0
    }

    fn is_timer_int_active(&self, index: usize) -> bool {
        self.int_status.get() & (1 << index) != 0
    }

    fn get_ticks(&self) -> u64 {
        ns_to_ticks(CLOCK_VIRTUAL.get_ns() + self.hpet_offset.get())
    }

    fn get_ns(&self, tick: u64) -> u64 {
        ticks_to_ns(tick) - self.hpet_offset.get()
    }

    fn handle_legacy_irq(&self, irq: u32, level: u32) {
        if irq == HPET_LEGACY_PIT_INT {
            if !self.is_legacy_mode() {
                self.irqs[0].set(level != 0);
            }
        } else {
            self.rtc_irq_level.set(level);
            if !self.is_legacy_mode() {
                self.irqs[RTC_ISA_IRQ].set(level != 0);
            }
        }
    }

    fn init_timers(this: &mut MaybeUninit<Self>) {
        let state = this.as_ptr();
        for index in 0..HPET_MAX_TIMERS {
            let mut timer = uninit_field_mut!(*this, timers[index]);

            // Initialize in two steps, to avoid calling Timer::init_full on a
            // temporary that can be moved.
            let timer = timer.write(BqlRefCell::new(HPETTimer::new(
                index.try_into().unwrap(),
                state,
            )));
            HPETTimer::init_timer_with_cell(timer);
        }
    }

    fn update_int_status(&self, index: u32, level: bool) {
        self.int_status
            .set(self.int_status.get().deposit(index, 1, u64::from(level)));
    }

    /// General Configuration Register
    fn set_cfg_reg(&self, shift: u32, len: u32, val: u64) {
        let old_val = self.config.get();
        let mut new_val = old_val.deposit(shift, len, val);

        new_val = hpet_fixup_reg(new_val, old_val, HPET_CFG_WRITE_MASK);
        self.config.set(new_val);

        if activating_bit(old_val, new_val, HPET_CFG_ENABLE_SHIFT) {
            // Enable main counter and interrupt generation.
            self.hpet_offset
                .set(ticks_to_ns(self.counter.get()) - CLOCK_VIRTUAL.get_ns());

            for timer in self.timers.iter().take(self.num_timers) {
                let mut t = timer.borrow_mut();

                if t.is_int_enabled() && t.is_int_active() {
                    t.update_irq(true);
                }
                t.set_timer();
            }
        } else if deactivating_bit(old_val, new_val, HPET_CFG_ENABLE_SHIFT) {
            // Halt main counter and disable interrupt generation.
            self.counter.set(self.get_ticks());

            for timer in self.timers.iter().take(self.num_timers) {
                timer.borrow_mut().del_timer();
            }
        }

        // i8254 and RTC output pins are disabled when HPET is in legacy mode
        if activating_bit(old_val, new_val, HPET_CFG_LEG_RT_SHIFT) {
            self.pit_enabled.set(false);
            self.irqs[0].lower();
            self.irqs[RTC_ISA_IRQ].lower();
        } else if deactivating_bit(old_val, new_val, HPET_CFG_LEG_RT_SHIFT) {
            // TODO: Add irq binding: qemu_irq_lower(s->irqs[0])
            self.irqs[0].lower();
            self.pit_enabled.set(true);
            self.irqs[RTC_ISA_IRQ].set(self.rtc_irq_level.get() != 0);
        }
    }

    /// General Interrupt Status Register: Read/Write Clear
    fn set_int_status_reg(&self, shift: u32, _len: u32, val: u64) {
        let new_val = val << shift;
        let cleared = new_val & self.int_status.get();

        for (index, timer) in self.timers.iter().take(self.num_timers).enumerate() {
            if cleared & (1 << index) != 0 {
                timer.borrow_mut().update_irq(false);
            }
        }
    }

    /// Main Counter Value Register
    fn set_counter_reg(&self, shift: u32, len: u32, val: u64) {
        if self.is_hpet_enabled() {
            // TODO: Add trace point -
            // trace_hpet_ram_write_counter_write_while_enabled()
            //
            // HPET spec says that writes to this register should only be
            // done while the counter is halted. So this is an undefined
            // behavior. There's no need to forbid it, but when HPET is
            // enabled, the changed counter value will not affect the
            // tick count (i.e., the previously calculated offset will
            // not be changed as well).
        }
        self.counter
            .set(self.counter.get().deposit(shift, len, val));
    }

    unsafe fn init(mut this: ParentInit<Self>) {
        static HPET_RAM_OPS: MemoryRegionOps<HPETState> =
            MemoryRegionOpsBuilder::<HPETState>::new()
                .read(&HPETState::read)
                .write(&HPETState::write)
                .native_endian()
                .valid_sizes(4, 8)
                .impl_sizes(4, 8)
                .build();

        MemoryRegion::init_io(
            &mut uninit_field_mut!(*this, iomem),
            &HPET_RAM_OPS,
            "hpet",
            HPET_REG_SPACE_LEN,
        );

        Self::init_timers(&mut this);
    }

    fn post_init(&self) {
        self.init_mmio(&self.iomem);
        for irq in self.irqs.iter() {
            self.init_irq(irq);
        }
    }

    fn realize(&self) -> util::Result<()> {
        ensure!(
            (HPET_MIN_TIMERS..=HPET_MAX_TIMERS).contains(&self.num_timers),
            "hpet.num_timers must be between {HPET_MIN_TIMERS} and {HPET_MAX_TIMERS}"
        );
        ensure!(
            self.int_route_cap != 0,
            "hpet.hpet-intcap property not initialized"
        );

        self.hpet_id.set(HPETFwConfig::assign_hpet_id()?);

        // 64-bit General Capabilities and ID Register; LegacyReplacementRoute.
        self.capability.set(
            HPET_CAP_REV_ID_VALUE << HPET_CAP_REV_ID_SHIFT |
            1 << HPET_CAP_COUNT_SIZE_CAP_SHIFT |
            1 << HPET_CAP_LEG_RT_CAP_SHIFT |
            HPET_CAP_VENDER_ID_VALUE << HPET_CAP_VENDER_ID_SHIFT |
            ((self.num_timers - 1) as u64) << HPET_CAP_NUM_TIM_SHIFT | // indicate the last timer
            (HPET_CLK_PERIOD * FS_PER_NS) << HPET_CAP_CNT_CLK_PERIOD_SHIFT, // 10 ns
        );

        self.init_gpio_in(2, HPETState::handle_legacy_irq);
        self.init_gpio_out(from_ref(&self.pit_enabled));
        Ok(())
    }

    fn reset_hold(&self, _type: ResetType) {
        for timer in self.timers.iter().take(self.num_timers) {
            timer.borrow_mut().reset();
        }

        self.counter.set(0);
        self.config.set(0);
        self.pit_enabled.set(true);
        self.hpet_offset.set(0);

        HPETFwConfig::update_hpet_cfg(
            self.hpet_id.get(),
            self.capability.get() as u32,
            self.mmio_addr(0).unwrap(),
        );

        // to document that the RTC lowers its output on reset as well
        self.rtc_irq_level.set(0);
    }

    fn decode(&self, mut addr: hwaddr, size: u32) -> HPETAddrDecode<'_> {
        let shift = ((addr & 4) * 8) as u32;
        let len = std::cmp::min(size * 8, 64 - shift);

        addr &= !4;
        let reg = if (0..=0xff).contains(&addr) {
            GlobalRegister::try_from(addr).map(HPETRegister::Global)
        } else {
            let timer_id: usize = ((addr - 0x100) / 0x20) as usize;
            if timer_id < self.num_timers {
                // TODO: Add trace point - trace_hpet_ram_[read|write]_timer_id(timer_id)
                TimerRegister::try_from(addr & 0x18)
                    .map(|reg| HPETRegister::Timer(&self.timers[timer_id], reg))
            } else {
                // TODO: Add trace point -  trace_hpet_timer_id_out_of_range(timer_id)
                Err(addr)
            }
        };

        // reg is now a Result<HPETRegister, hwaddr>
        // convert the Err case into HPETRegister as well
        let reg = reg.unwrap_or_else(HPETRegister::Unknown);
        HPETAddrDecode { shift, len, reg }
    }

    fn read(&self, addr: hwaddr, size: u32) -> u64 {
        // TODO: Add trace point - trace_hpet_ram_read(addr)
        let HPETAddrDecode { shift, reg, .. } = self.decode(addr, size);

        use GlobalRegister::*;
        use HPETRegister::*;
        (match reg {
            Timer(timer, tn_reg) => timer.borrow_mut().read(tn_reg),
            Global(CAP) => self.capability.get(), /* including HPET_PERIOD 0x004 */
            Global(CFG) => self.config.get(),
            Global(INT_STATUS) => self.int_status.get(),
            Global(COUNTER) => {
                // TODO: Add trace point
                // trace_hpet_ram_read_reading_counter(addr & 4, cur_tick)
                if self.is_hpet_enabled() {
                    self.get_ticks()
                } else {
                    self.counter.get()
                }
            }
            Unknown(_) => {
                // TODO: Add trace point- trace_hpet_ram_read_invalid()
                0
            }
        }) >> shift
    }

    fn write(&self, addr: hwaddr, value: u64, size: u32) {
        let HPETAddrDecode { shift, len, reg } = self.decode(addr, size);

        // TODO: Add trace point - trace_hpet_ram_write(addr, value)
        use GlobalRegister::*;
        use HPETRegister::*;
        match reg {
            Timer(timer, tn_reg) => timer.borrow_mut().write(tn_reg, value, shift, len),
            Global(CAP) => {} // General Capabilities and ID Register: Read Only
            Global(CFG) => self.set_cfg_reg(shift, len, value),
            Global(INT_STATUS) => self.set_int_status_reg(shift, len, value),
            Global(COUNTER) => self.set_counter_reg(shift, len, value),
            Unknown(_) => {
                // TODO: Add trace point - trace_hpet_ram_write_invalid()
            }
        }
    }

    fn pre_save(&self) -> Result<(), migration::Infallible> {
        if self.is_hpet_enabled() {
            self.counter.set(self.get_ticks());
        }

        /*
         * The number of timers must match on source and destination, but it was
         * also added to the migration stream.  Check that it matches the value
         * that was configured.
         */
        self.num_timers_save.set(self.num_timers as u8);
        Ok(())
    }

    fn post_load(&self, _version_id: u8) -> Result<(), migration::Infallible> {
        for timer in self.timers.iter().take(self.num_timers) {
            let mut t = timer.borrow_mut();

            t.cmp64 = t.calculate_cmp64(t.get_state().counter.get(), t.cmp);
            t.last = CLOCK_VIRTUAL.get_ns() - NANOSECONDS_PER_SECOND;
        }

        // Recalculate the offset between the main counter and guest time
        if !self.hpet_offset_saved {
            self.hpet_offset
                .set(ticks_to_ns(self.counter.get()) - CLOCK_VIRTUAL.get_ns());
        }

        Ok(())
    }

    fn is_rtc_irq_level_needed(&self) -> bool {
        self.rtc_irq_level.get() != 0
    }

    fn is_offset_needed(&self) -> bool {
        self.is_hpet_enabled() && self.hpet_offset_saved
    }

    fn validate_num_timers(&self, _version_id: u8) -> bool {
        self.num_timers == self.num_timers_save.get().into()
    }
}

qom_isa!(HPETState: SysBusDevice, DeviceState, Object);

unsafe impl ObjectType for HPETState {
    // No need for HPETClass. Just like OBJECT_DECLARE_SIMPLE_TYPE in C.
    type Class = <SysBusDevice as ObjectType>::Class;
    const TYPE_NAME: &'static CStr = crate::TYPE_HPET;
}

impl ObjectImpl for HPETState {
    type ParentType = SysBusDevice;

    const INSTANCE_INIT: Option<unsafe fn(ParentInit<Self>)> = Some(Self::init);
    const INSTANCE_POST_INIT: Option<fn(&Self)> = Some(Self::post_init);
    const CLASS_INIT: fn(&mut Self::Class) = Self::Class::class_init::<Self>;
}

static VMSTATE_HPET_RTC_IRQ_LEVEL: VMStateDescription<HPETState> =
    VMStateDescriptionBuilder::<HPETState>::new()
        .name(c"hpet/rtc_irq_level")
        .version_id(1)
        .minimum_version_id(1)
        .needed(&HPETState::is_rtc_irq_level_needed)
        .fields(vmstate_fields! {
            vmstate_of!(HPETState, rtc_irq_level),
        })
        .build();

static VMSTATE_HPET_OFFSET: VMStateDescription<HPETState> =
    VMStateDescriptionBuilder::<HPETState>::new()
        .name(c"hpet/offset")
        .version_id(1)
        .minimum_version_id(1)
        .needed(&HPETState::is_offset_needed)
        .fields(vmstate_fields! {
            vmstate_of!(HPETState, hpet_offset),
        })
        .build();

const VMSTATE_HPET_TIMER: VMStateDescription<HPETTimer> =
    VMStateDescriptionBuilder::<HPETTimer>::new()
        .name(c"hpet_timer")
        .version_id(1)
        .minimum_version_id(1)
        .fields(vmstate_fields! {
            vmstate_of!(HPETTimer, index),
            vmstate_of!(HPETTimer, config),
            vmstate_of!(HPETTimer, cmp),
            vmstate_of!(HPETTimer, fsb),
            vmstate_of!(HPETTimer, period),
            vmstate_of!(HPETTimer, wrap_flag),
            vmstate_of!(HPETTimer, qemu_timer),
        })
        .build();
impl_vmstate_struct!(HPETTimer, VMSTATE_HPET_TIMER);

const VALIDATE_TIMERS_NAME: &CStr = c"num_timers must match";

const VMSTATE_HPET: VMStateDescription<HPETState> =
    VMStateDescriptionBuilder::<HPETState>::new()
        .name(c"hpet")
        .version_id(2)
        .minimum_version_id(2)
        .pre_save(&HPETState::pre_save)
        .post_load(&HPETState::post_load)
        .fields(vmstate_fields! {
            vmstate_of!(HPETState, config),
            vmstate_of!(HPETState, int_status),
            vmstate_of!(HPETState, counter),
            vmstate_of!(HPETState, num_timers_save),
            vmstate_validate!(HPETState, VALIDATE_TIMERS_NAME, HPETState::validate_num_timers),
            vmstate_of!(HPETState, timers[0 .. num_timers_save], HPETState::validate_num_timers).with_version_id(0),
        })
        .subsections(vmstate_subsections!(
            VMSTATE_HPET_RTC_IRQ_LEVEL,
            VMSTATE_HPET_OFFSET,
        ))
        .build();

impl DeviceImpl for HPETState {
    const VMSTATE: Option<VMStateDescription<Self>> = Some(VMSTATE_HPET);
    const REALIZE: Option<fn(&Self) -> util::Result<()>> = Some(Self::realize);
}

impl ResettablePhasesImpl for HPETState {
    const HOLD: Option<fn(&Self, ResetType)> = Some(Self::reset_hold);
}

impl SysBusDeviceImpl for HPETState {}
