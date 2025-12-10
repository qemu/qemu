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

use bql::prelude::*;
use common::prelude::*;
use hwcore::prelude::*;
use migration::{self, prelude::*, ToMigrationStateShared};
use qom::prelude::*;
use system::{
    bindings::{address_space_memory, address_space_stl_le},
    prelude::*,
    MEMTXATTRS_UNSPECIFIED,
};
use util::prelude::*;

use crate::fw_cfg::HPETFwConfig;

::trace::include_trace!("hw_timer");

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
/// Timer register enumerations, masked by 0x18
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
/// Global register enumerations
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

enum DecodedRegister<'a> {
    /// Global register in the range from `0` to `0xff`
    Global(GlobalRegister),

    /// Register in the timer block `0x100`...`0x3ff`
    Timer(&'a HPETTimer, TimerRegister),

    /// Invalid address
    #[allow(dead_code)]
    Unknown(hwaddr),
}

struct HPETAddrDecode<'a> {
    shift: u32,
    len: u32,
    target: DecodedRegister<'a>,
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

fn timer_handler(t: &HPETTimer) {
    // SFAETY: state field is valid after timer initialization.
    let hpet_regs = &unsafe { t.state.as_ref() }.regs;
    t.callback(&mut hpet_regs.borrow_mut())
}

#[derive(Debug, Default)]
pub struct HPETTimerRegisters {
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
    wrap_flag: bool,
    /// last value armed, to avoid timer storms
    last: u64,
}

impl HPETTimerRegisters {
    /// calculate next value of the general counter that matches the
    /// target (either entirely, or the low 32-bit only depending on
    /// the timer mode).
    fn update_cmp64(&mut self, cur_tick: u64) {
        self.cmp64 = if self.is_32bit_mod() {
            let mut result: u64 = cur_tick.deposit(0, 32, self.cmp);
            if result < cur_tick {
                result += 0x100000000;
            }
            result
        } else {
            self.cmp
        }
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

    /// True if timer interrupt is level triggered; otherwise, edge triggered.
    const fn is_int_level_triggered(&self) -> bool {
        self.config & (1 << HPET_TN_CFG_INT_TYPE_SHIFT) != 0
    }

    const fn clear_valset(&mut self) {
        self.config &= !(1 << HPET_TN_CFG_SETVAL_SHIFT);
    }

    const fn get_individual_route(&self) -> usize {
        ((self.config & HPET_TN_CFG_INT_ROUTE_MASK) >> HPET_TN_CFG_INT_ROUTE_SHIFT) as usize
    }
}

/// HPET Timer Abstraction
#[derive(Debug)]
pub struct HPETTimer {
    /// timer N index within the timer block (`HPETState`)
    #[doc(alias = "tn")]
    index: u8,
    qemu_timer: Timer,
    /// timer block abstraction containing this timer
    state: NonNull<HPETState>,
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
        }
    }

    fn init_timer(timer: Pin<&mut Self>) {
        Timer::init_full(
            timer,
            None,
            CLOCK_VIRTUAL,
            Timer::NS,
            0,
            timer_handler,
            |t| &mut t.qemu_timer,
        );
    }

    fn get_state(&self) -> &HPETState {
        // SAFETY:
        // the pointer is convertible to a reference
        unsafe { self.state.as_ref() }
    }

    fn is_int_active(&self, regs: &HPETRegisters) -> bool {
        regs.is_timer_int_active(self.index.into())
    }

    fn get_int_route(&self, regs: &HPETRegisters) -> usize {
        if self.index <= 1 && regs.is_legacy_mode() {
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
            regs.tn_regs[self.index as usize].get_individual_route()
        }
    }

    fn set_irq(&self, regs: &HPETRegisters, set: bool) {
        let tn_regs = &regs.tn_regs[self.index as usize];
        let route = self.get_int_route(regs);

        if set && tn_regs.is_int_enabled() && regs.is_hpet_enabled() {
            if tn_regs.is_fsb_route_enabled() {
                // SAFETY:
                // the parameters are valid.
                unsafe {
                    address_space_stl_le(
                        addr_of_mut!(address_space_memory),
                        tn_regs.fsb >> 32,  // Timer N FSB int addr
                        tn_regs.fsb as u32, // Timer N FSB int value, truncate!
                        MEMTXATTRS_UNSPECIFIED,
                        null_mut(),
                    );
                }
            } else if tn_regs.is_int_level_triggered() {
                self.get_state().irqs[route].raise();
            } else {
                self.get_state().irqs[route].pulse();
            }
        } else if !tn_regs.is_fsb_route_enabled() {
            self.get_state().irqs[route].lower();
        }
    }

    fn update_irq(&self, regs: &mut HPETRegisters, set: bool) {
        // If Timer N Interrupt Enable bit is 0, "the timer will
        // still operate and generate appropriate status bits, but
        // will not cause an interrupt"
        regs.int_status = regs.int_status.deposit(
            self.index.into(),
            1,
            u64::from(set && regs.tn_regs[self.index as usize].is_int_level_triggered()),
        );
        self.set_irq(regs, set);
    }

    fn arm_timer(&self, regs: &mut HPETRegisters, tick: u64) {
        let mut ns = regs.get_ns(tick);
        let tn_regs = &mut regs.tn_regs[self.index as usize];

        // Clamp period to reasonable min value (1 us)
        if tn_regs.is_periodic() && ns - tn_regs.last < 1000 {
            ns = tn_regs.last + 1000;
        }

        tn_regs.last = ns;
        self.qemu_timer.modify(tn_regs.last);
    }

    fn set_timer(&self, regs: &mut HPETRegisters) {
        let cur_tick: u64 = regs.get_ticks();
        let tn_regs = &mut regs.tn_regs[self.index as usize];

        tn_regs.wrap_flag = false;
        tn_regs.update_cmp64(cur_tick);

        let mut next_tick: u64 = tn_regs.cmp64;
        if tn_regs.is_32bit_mod() {
            // HPET spec says in one-shot 32-bit mode, generate an interrupt when
            // counter wraps in addition to an interrupt with comparator match.
            if !tn_regs.is_periodic() && tn_regs.cmp64 > hpet_next_wrap(cur_tick) {
                tn_regs.wrap_flag = true;
                next_tick = hpet_next_wrap(cur_tick);
            }
        }
        self.arm_timer(regs, next_tick);
    }

    fn del_timer(&self, regs: &mut HPETRegisters) {
        // Just remove the timer from the timer_list without destroying
        // this timer instance.
        self.qemu_timer.delete();

        if self.is_int_active(regs) {
            // For level-triggered interrupt, this leaves interrupt status
            // register set but lowers irq.
            self.update_irq(regs, true);
        }
    }

    fn prepare_tn_cfg_reg_new(
        &self,
        regs: &mut HPETRegisters,
        shift: u32,
        len: u32,
        val: u64,
    ) -> (u64, u64) {
        trace::trace_hpet_ram_write_tn_cfg((shift / 8).try_into().unwrap());
        let tn_regs = &regs.tn_regs[self.index as usize];
        let old_val: u64 = tn_regs.config;
        let mut new_val: u64 = old_val.deposit(shift, len, val);
        new_val = hpet_fixup_reg(new_val, old_val, HPET_TN_CFG_WRITE_MASK);

        // Switch level-type interrupt to edge-type.
        if deactivating_bit(old_val, new_val, HPET_TN_CFG_INT_TYPE_SHIFT) {
            // Do this before changing timer.regs.config; otherwise, if
            // HPET_TN_FSB is set, update_irq will not lower the qemu_irq.
            self.update_irq(regs, false);
        }

        (new_val, old_val)
    }

    /// Configuration and Capability Register
    fn set_tn_cfg_reg(&self, regs: &mut HPETRegisters, shift: u32, len: u32, val: u64) {
        // Factor out a prepare_tn_cfg_reg_new() to better handle immutable scope.
        let (new_val, old_val) = self.prepare_tn_cfg_reg_new(regs, shift, len, val);
        regs.tn_regs[self.index as usize].config = new_val;

        if activating_bit(old_val, new_val, HPET_TN_CFG_INT_ENABLE_SHIFT)
            && self.is_int_active(regs)
        {
            self.update_irq(regs, true);
        }

        let tn_regs = &mut regs.tn_regs[self.index as usize];
        if tn_regs.is_32bit_mod() {
            tn_regs.cmp = u64::from(tn_regs.cmp as u32); // truncate!
            tn_regs.period = u64::from(tn_regs.period as u32); // truncate!
        }

        if regs.is_hpet_enabled() {
            self.set_timer(regs);
        }
    }

    /// Comparator Value Register
    fn set_tn_cmp_reg(&self, regs: &mut HPETRegisters, shift: u32, len: u32, val: u64) {
        let tn_regs = &mut regs.tn_regs[self.index as usize];
        let mut length = len;
        let mut value = val;

        if tn_regs.is_32bit_mod() {
            // High 32-bits are zero, leave them untouched.
            if shift != 0 {
                trace::trace_hpet_ram_write_invalid_tn_cmp();
                return;
            }
            length = 64;
            value = u64::from(value as u32); // truncate!
        }

        trace::trace_hpet_ram_write_tn_cmp((shift / 8).try_into().unwrap());

        if !tn_regs.is_periodic() || tn_regs.is_valset_enabled() {
            tn_regs.cmp = tn_regs.cmp.deposit(shift, length, value);
        }

        if tn_regs.is_periodic() {
            tn_regs.period = tn_regs.period.deposit(shift, length, value);
        }

        tn_regs.clear_valset();
        if regs.is_hpet_enabled() {
            self.set_timer(regs);
        }
    }

    /// FSB Interrupt Route Register
    fn set_tn_fsb_route_reg(&self, regs: &mut HPETRegisters, shift: u32, len: u32, val: u64) {
        let tn_regs = &mut regs.tn_regs[self.index as usize];
        tn_regs.fsb = tn_regs.fsb.deposit(shift, len, val);
    }

    fn reset(&self, regs: &mut HPETRegisters) {
        self.del_timer(regs);

        let tn_regs = &mut regs.tn_regs[self.index as usize];
        tn_regs.cmp = u64::MAX; // Comparator Match Registers reset to all 1's.
        tn_regs.config = (1 << HPET_TN_CFG_PERIODIC_CAP_SHIFT) | (1 << HPET_TN_CFG_SIZE_CAP_SHIFT);
        if self.get_state().has_msi_flag() {
            tn_regs.config |= 1 << HPET_TN_CFG_FSB_CAP_SHIFT;
        }
        // advertise availability of ioapic int
        tn_regs.config |=
            (u64::from(self.get_state().int_route_cap)) << HPET_TN_CFG_INT_ROUTE_CAP_SHIFT;
        tn_regs.period = 0;
        tn_regs.wrap_flag = false;
    }

    /// timer expiration callback
    fn callback(&self, regs: &mut HPETRegisters) {
        let cur_tick: u64 = regs.get_ticks();
        let tn_regs = &mut regs.tn_regs[self.index as usize];

        let next_tick = if tn_regs.is_periodic() && tn_regs.period != 0 {
            while hpet_time_after(cur_tick, tn_regs.cmp64) {
                tn_regs.cmp64 += tn_regs.period;
            }
            if tn_regs.is_32bit_mod() {
                tn_regs.cmp = u64::from(tn_regs.cmp64 as u32); // truncate!
            } else {
                tn_regs.cmp = tn_regs.cmp64;
            }
            Some(tn_regs.cmp64)
        } else {
            tn_regs.wrap_flag.then_some(tn_regs.cmp64)
        };

        tn_regs.wrap_flag = false;
        if let Some(tick) = next_tick {
            self.arm_timer(regs, tick);
        }
        self.update_irq(regs, true);
    }

    fn read(&self, target: TimerRegister, regs: &HPETRegisters) -> u64 {
        let tn_regs = &regs.tn_regs[self.index as usize];

        use TimerRegister::*;
        match target {
            CFG => tn_regs.config, // including interrupt capabilities
            CMP => tn_regs.cmp,    // comparator register
            ROUTE => tn_regs.fsb,
        }
    }

    fn write(
        &self,
        target: TimerRegister,
        regs: &mut HPETRegisters,
        value: u64,
        shift: u32,
        len: u32,
    ) {
        use TimerRegister::*;

        trace::trace_hpet_ram_write_timer_id(self.index);
        match target {
            CFG => self.set_tn_cfg_reg(regs, shift, len, value),
            CMP => self.set_tn_cmp_reg(regs, shift, len, value),
            ROUTE => self.set_tn_fsb_route_reg(regs, shift, len, value),
        }
    }
}

#[derive(Default, ToMigrationState)]
pub struct HPETRegisters {
    // HPET block Registers: Memory-mapped, software visible registers
    /// General Capabilities and ID Register
    ///
    /// Constant and therefore not migrated.
    #[migration_state(omit)]
    capability: u64,
    ///  General Configuration Register
    config: u64,
    /// General Interrupt Status Register
    #[doc(alias = "isr")]
    int_status: u64,
    /// Main Counter Value Register
    #[doc(alias = "hpet_counter")]
    counter: u64,

    /// HPET Timer N Registers
    ///
    /// Migrated as part of `Migratable<HPETTimer>`
    #[migration_state(omit)]
    tn_regs: [HPETTimerRegisters; HPET_MAX_TIMERS],

    /// Offset of main counter relative to qemu clock.
    ///
    /// Migrated as a subsection and therefore snapshotted into [`HPETState`]
    #[migration_state(omit)]
    pub hpet_offset: u64,
}

impl HPETRegisters {
    fn get_ticks(&self) -> u64 {
        ns_to_ticks(CLOCK_VIRTUAL.get_ns() + self.hpet_offset)
    }

    fn get_ns(&self, tick: u64) -> u64 {
        ticks_to_ns(tick) - self.hpet_offset
    }

    fn is_legacy_mode(&self) -> bool {
        self.config & (1 << HPET_CFG_LEG_RT_SHIFT) != 0
    }

    fn is_hpet_enabled(&self) -> bool {
        self.config & (1 << HPET_CFG_ENABLE_SHIFT) != 0
    }

    fn is_timer_int_active(&self, index: usize) -> bool {
        self.int_status & (1 << index) != 0
    }
}

/// HPET Event Timer Block Abstraction
#[repr(C)]
#[derive(qom::Object, hwcore::Device)]
pub struct HPETState {
    parent_obj: ParentField<SysBusDevice>,
    iomem: MemoryRegion,
    regs: Migratable<BqlRefCell<HPETRegisters>>,

    // Internal state
    /// Capabilities that QEMU HPET supports.
    /// bit 0: MSI (or FSB) support.
    #[property(rename = "msi", bit = HPET_FLAG_MSI_SUPPORT_SHIFT, default = false)]
    flags: u32,

    hpet_offset_migration: BqlCell<u64>,
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
    timers: [Migratable<HPETTimer>; HPET_MAX_TIMERS],
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

    fn handle_legacy_irq(&self, irq: u32, level: u32) {
        let regs = self.regs.borrow();
        if irq == HPET_LEGACY_PIT_INT {
            if !regs.is_legacy_mode() {
                self.irqs[0].set(level != 0);
            }
        } else {
            self.rtc_irq_level.set(level);
            if !regs.is_legacy_mode() {
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
            let timer = timer.write(Migratable::new(HPETTimer::new(
                index.try_into().unwrap(),
                state,
            )));
            // SAFETY: HPETState is pinned
            let timer = unsafe { Pin::new_unchecked(&mut **timer) };
            HPETTimer::init_timer(timer);
        }
    }

    /// General Configuration Register
    fn set_cfg_reg(&self, regs: &mut HPETRegisters, shift: u32, len: u32, val: u64) {
        let old_val = regs.config;
        let mut new_val = old_val.deposit(shift, len, val);

        new_val = hpet_fixup_reg(new_val, old_val, HPET_CFG_WRITE_MASK);
        regs.config = new_val;

        if activating_bit(old_val, new_val, HPET_CFG_ENABLE_SHIFT) {
            // Enable main counter and interrupt generation.
            regs.hpet_offset = ticks_to_ns(regs.counter) - CLOCK_VIRTUAL.get_ns();

            for t in self.timers.iter().take(self.num_timers) {
                let id = t.index as usize;
                let tn_regs = &regs.tn_regs[id];

                if tn_regs.is_int_enabled() && t.is_int_active(regs) {
                    t.update_irq(regs, true);
                }
                t.set_timer(regs);
            }
        } else if deactivating_bit(old_val, new_val, HPET_CFG_ENABLE_SHIFT) {
            // Halt main counter and disable interrupt generation.
            regs.counter = regs.get_ticks();

            for t in self.timers.iter().take(self.num_timers) {
                t.del_timer(regs);
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
    fn set_int_status_reg(&self, regs: &mut HPETRegisters, shift: u32, _len: u32, val: u64) {
        let new_val = val << shift;
        let cleared = new_val & regs.int_status;

        for t in self.timers.iter().take(self.num_timers) {
            if cleared & (1 << t.index) != 0 {
                t.update_irq(regs, false);
            }
        }
    }

    /// Main Counter Value Register
    fn set_counter_reg(&self, regs: &mut HPETRegisters, shift: u32, len: u32, val: u64) {
        if regs.is_hpet_enabled() {
            // HPET spec says that writes to this register should only be
            // done while the counter is halted. So this is an undefined
            // behavior. There's no need to forbid it, but when HPET is
            // enabled, the changed counter value will not affect the
            // tick count (i.e., the previously calculated offset will
            // not be changed as well).
            trace::trace_hpet_ram_write_counter_write_while_enabled();
        }
        regs.counter = regs.counter.deposit(shift, len, val);
    }

    unsafe fn init(mut this: ParentInit<Self>) {
        static HPET_RAM_OPS: MemoryRegionOps<HPETState> =
            MemoryRegionOpsBuilder::<HPETState>::new()
                .read(&HPETState::read)
                .write(&HPETState::write)
                .little_endian()
                .valid_sizes(4, 8)
                .impl_sizes(4, 8)
                .build();

        MemoryRegion::init_io(
            &mut uninit_field_mut!(*this, iomem),
            &HPET_RAM_OPS,
            "hpet",
            HPET_REG_SPACE_LEN,
        );

        // Only consider members with more complex structures. C has already
        // initialized memory to all zeros - simple types (bool/u32/usize) can
        // rely on this without explicit initialization.
        uninit_field_mut!(*this, regs).write(Default::default());
        uninit_field_mut!(*this, hpet_offset_migration).write(Default::default());
        // Set null_mut for now and post_init() will fill it.
        uninit_field_mut!(*this, irqs).write(Default::default());
        uninit_field_mut!(*this, rtc_irq_level).write(Default::default());
        uninit_field_mut!(*this, pit_enabled).write(Default::default());
        uninit_field_mut!(*this, num_timers_save).write(Default::default());
        uninit_field_mut!(*this, hpet_id).write(Default::default());

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
        self.regs.borrow_mut().capability = HPET_CAP_REV_ID_VALUE << HPET_CAP_REV_ID_SHIFT |
            1 << HPET_CAP_COUNT_SIZE_CAP_SHIFT |
            1 << HPET_CAP_LEG_RT_CAP_SHIFT |
            HPET_CAP_VENDER_ID_VALUE << HPET_CAP_VENDER_ID_SHIFT |
            ((self.num_timers - 1) as u64) << HPET_CAP_NUM_TIM_SHIFT | // indicate the last timer
            (HPET_CLK_PERIOD * FS_PER_NS) << HPET_CAP_CNT_CLK_PERIOD_SHIFT; // 10 ns

        self.init_gpio_in(2, HPETState::handle_legacy_irq);
        self.init_gpio_out(from_ref(&self.pit_enabled));
        Ok(())
    }

    fn reset_hold(&self, _type: ResetType) {
        let mut regs = self.regs.borrow_mut();
        for t in self.timers.iter().take(self.num_timers) {
            t.reset(&mut regs);
        }

        regs.counter = 0;
        regs.config = 0;
        regs.hpet_offset = 0;
        HPETFwConfig::update_hpet_cfg(
            self.hpet_id.get(),
            regs.capability as u32,
            self.mmio_addr(0).unwrap(),
        );

        // pit_enabled.set(true) will call irq handler and access regs
        // again. We cannot borrow BqlRefCell twice at once. Minimize the
        // scope of regs to ensure it will be dropped before irq callback.
        drop(regs);

        self.pit_enabled.set(true);

        // to document that the RTC lowers its output on reset as well
        self.rtc_irq_level.set(0);
    }

    fn decode(&self, mut addr: hwaddr, size: u32) -> HPETAddrDecode<'_> {
        let shift = ((addr & 4) * 8) as u32;
        let len = std::cmp::min(size * 8, 64 - shift);

        addr &= !4;
        let target = if (0..=0xff).contains(&addr) {
            GlobalRegister::try_from(addr).map(DecodedRegister::Global)
        } else {
            let timer_id: usize = ((addr - 0x100) / 0x20) as usize;
            if timer_id < self.num_timers {
                TimerRegister::try_from(addr & 0x18)
                    .map(|target| DecodedRegister::Timer(&self.timers[timer_id], target))
            } else {
                trace::trace_hpet_timer_id_out_of_range(timer_id.try_into().unwrap());
                Err(addr)
            }
        };

        // `target` is now a Result<DecodedRegister, hwaddr>
        // convert the Err case into DecodedRegister as well
        let target = target.unwrap_or_else(DecodedRegister::Unknown);
        HPETAddrDecode { shift, len, target }
    }

    fn read(&self, addr: hwaddr, size: u32) -> u64 {
        trace::trace_hpet_ram_read(addr);

        let HPETAddrDecode { shift, target, .. } = self.decode(addr, size);
        let regs = &self.regs.borrow();

        use DecodedRegister::*;
        use GlobalRegister::*;
        (match target {
            Timer(t, tn_target) => t.read(tn_target, regs),
            Global(CAP) => regs.capability, /* including HPET_PERIOD 0x004 */
            Global(CFG) => regs.config,
            Global(INT_STATUS) => regs.int_status,
            Global(COUNTER) => {
                let cur_tick = if regs.is_hpet_enabled() {
                    regs.get_ticks()
                } else {
                    regs.counter
                };

                trace::trace_hpet_ram_read_reading_counter((addr & 4) as u8, cur_tick);

                cur_tick
            }
            Unknown(_) => {
                trace::trace_hpet_ram_read_invalid();
                0
            }
        }) >> shift
    }

    fn write(&self, addr: hwaddr, value: u64, size: u32) {
        let HPETAddrDecode { shift, len, target } = self.decode(addr, size);
        let mut regs = self.regs.borrow_mut();

        trace::trace_hpet_ram_write(addr, value);

        use DecodedRegister::*;
        use GlobalRegister::*;
        match target {
            Timer(t, tn_target) => t.write(tn_target, &mut regs, value, shift, len),
            Global(CAP) => {} // General Capabilities and ID Register: Read Only
            Global(CFG) => self.set_cfg_reg(&mut regs, shift, len, value),
            Global(INT_STATUS) => self.set_int_status_reg(&mut regs, shift, len, value),
            Global(COUNTER) => self.set_counter_reg(&mut regs, shift, len, value),
            Unknown(_) => trace::trace_hpet_ram_write_invalid(),
        }
    }

    fn pre_save(&self) -> Result<(), migration::Infallible> {
        let mut regs = self.regs.borrow_mut();
        self.hpet_offset_migration.set(regs.hpet_offset);
        if regs.is_hpet_enabled() {
            regs.counter = regs.get_ticks();
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
        let mut regs = self.regs.borrow_mut();
        let cnt = regs.counter;

        for index in 0..self.num_timers {
            let tn_regs = &mut regs.tn_regs[index];

            tn_regs.update_cmp64(cnt);
            tn_regs.last = CLOCK_VIRTUAL.get_ns() - NANOSECONDS_PER_SECOND;
        }

        // Recalculate the offset between the main counter and guest time
        if !self.hpet_offset_saved {
            self.hpet_offset_migration
                .set(ticks_to_ns(regs.counter) - CLOCK_VIRTUAL.get_ns());
        }
        regs.hpet_offset = self.hpet_offset_migration.get();

        Ok(())
    }

    fn is_rtc_irq_level_needed(&self) -> bool {
        self.rtc_irq_level.get() != 0
    }

    fn is_offset_needed(&self) -> bool {
        self.regs.borrow().is_hpet_enabled() && self.hpet_offset_saved
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
            vmstate_of!(HPETState, hpet_offset_migration),
        })
        .build();

#[derive(Default)]
pub struct HPETTimerMigration {
    index: u8,
    config: u64,
    cmp: u64,
    fsb: u64,
    period: u64,
    wrap_flag: u8,
    qemu_timer: i64,
}

impl ToMigrationState for HPETTimer {
    type Migrated = HPETTimerMigration;

    fn snapshot_migration_state(
        &self,
        target: &mut Self::Migrated,
    ) -> Result<(), migration::InvalidError> {
        let state = self.get_state();
        let regs = state.regs.borrow_mut();
        let tn_regs = &regs.tn_regs[self.index as usize];

        target.index = self.index;
        target.config = tn_regs.config;
        target.cmp = tn_regs.cmp;
        target.fsb = tn_regs.fsb;
        target.period = tn_regs.period;
        target.wrap_flag = u8::from(tn_regs.wrap_flag);
        self.qemu_timer
            .snapshot_migration_state(&mut target.qemu_timer)?;

        Ok(())
    }

    fn restore_migrated_state_mut(
        &mut self,
        source: Self::Migrated,
        version_id: u8,
    ) -> Result<(), migration::InvalidError> {
        self.restore_migrated_state(source, version_id)
    }
}

impl ToMigrationStateShared for HPETTimer {
    fn restore_migrated_state(
        &self,
        source: Self::Migrated,
        version_id: u8,
    ) -> Result<(), migration::InvalidError> {
        let state = self.get_state();
        let mut regs = state.regs.borrow_mut();
        let tn_regs = &mut regs.tn_regs[self.index as usize];

        tn_regs.config = source.config;
        tn_regs.cmp = source.cmp;
        tn_regs.fsb = source.fsb;
        tn_regs.period = source.period;
        tn_regs.wrap_flag = source.wrap_flag != 0;
        self.qemu_timer
            .restore_migrated_state(source.qemu_timer, version_id)?;

        Ok(())
    }
}

const VMSTATE_HPET_TIMER: VMStateDescription<HPETTimerMigration> =
    VMStateDescriptionBuilder::<HPETTimerMigration>::new()
        .name(c"hpet_timer")
        .version_id(1)
        .minimum_version_id(1)
        .fields(vmstate_fields! {
            vmstate_of!(HPETTimerMigration, index),
            vmstate_of!(HPETTimerMigration, config),
            vmstate_of!(HPETTimerMigration, cmp),
            vmstate_of!(HPETTimerMigration, fsb),
            vmstate_of!(HPETTimerMigration, period),
            vmstate_of!(HPETTimerMigration, wrap_flag),
            vmstate_of!(HPETTimerMigration, qemu_timer),
        })
        .build();

impl_vmstate_struct!(HPETTimerMigration, VMSTATE_HPET_TIMER);

const VALIDATE_TIMERS_NAME: &CStr = c"num_timers must match";

// HPETRegistersMigration is generated by ToMigrationState macro.
impl_vmstate_struct!(
    HPETRegistersMigration,
    VMStateDescriptionBuilder::<HPETRegistersMigration>::new()
        .name(c"hpet/regs")
        .version_id(2)
        .minimum_version_id(2)
        .fields(vmstate_fields! {
            vmstate_of!(HPETRegistersMigration, config),
            vmstate_of!(HPETRegistersMigration, int_status),
            vmstate_of!(HPETRegistersMigration, counter),
        })
        .build()
);

const VMSTATE_HPET: VMStateDescription<HPETState> =
    VMStateDescriptionBuilder::<HPETState>::new()
        .name(c"hpet")
        .version_id(2)
        .minimum_version_id(2)
        .pre_save(&HPETState::pre_save)
        .post_load(&HPETState::post_load)
        .fields(vmstate_fields! {
            vmstate_of!(HPETState, regs),
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
