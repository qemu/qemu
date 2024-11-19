// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use core::ptr::{addr_of, addr_of_mut, NonNull};
use std::{
    ffi::CStr,
    os::raw::{c_int, c_uchar, c_uint, c_void},
};

use qemu_api::{
    bindings::{self, *},
    c_str,
    definitions::ObjectImpl,
    device_class::TYPE_SYS_BUS_DEVICE,
};

use crate::{
    memory_ops::PL011_OPS,
    registers::{self, Interrupt},
    RegisterOffset,
};

/// Integer Baud Rate Divider, `UARTIBRD`
const IBRD_MASK: u32 = 0xffff;

/// Fractional Baud Rate Divider, `UARTFBRD`
const FBRD_MASK: u32 = 0x3f;

const DATA_BREAK: u32 = 1 << 10;

/// QEMU sourced constant.
pub const PL011_FIFO_DEPTH: usize = 16_usize;

#[derive(Clone, Copy, Debug)]
enum DeviceId {
    #[allow(dead_code)]
    Arm = 0,
    Luminary,
}

impl std::ops::Index<hwaddr> for DeviceId {
    type Output = c_uchar;

    fn index(&self, idx: hwaddr) -> &Self::Output {
        match self {
            Self::Arm => &Self::PL011_ID_ARM[idx as usize],
            Self::Luminary => &Self::PL011_ID_LUMINARY[idx as usize],
        }
    }
}

impl DeviceId {
    const PL011_ID_ARM: [c_uchar; 8] = [0x11, 0x10, 0x14, 0x00, 0x0d, 0xf0, 0x05, 0xb1];
    const PL011_ID_LUMINARY: [c_uchar; 8] = [0x11, 0x00, 0x18, 0x01, 0x0d, 0xf0, 0x05, 0xb1];
}

#[repr(C)]
#[derive(Debug, qemu_api_macros::Object, qemu_api_macros::offsets)]
/// PL011 Device Model in QEMU
pub struct PL011State {
    pub parent_obj: SysBusDevice,
    pub iomem: MemoryRegion,
    #[doc(alias = "fr")]
    pub flags: registers::Flags,
    #[doc(alias = "lcr")]
    pub line_control: registers::LineControl,
    #[doc(alias = "rsr")]
    pub receive_status_error_clear: registers::ReceiveStatusErrorClear,
    #[doc(alias = "cr")]
    pub control: registers::Control,
    pub dmacr: u32,
    pub int_enabled: u32,
    pub int_level: u32,
    pub read_fifo: [u32; PL011_FIFO_DEPTH],
    pub ilpr: u32,
    pub ibrd: u32,
    pub fbrd: u32,
    pub ifl: u32,
    pub read_pos: usize,
    pub read_count: usize,
    pub read_trigger: usize,
    #[doc(alias = "chr")]
    pub char_backend: CharBackend,
    /// QEMU interrupts
    ///
    /// ```text
    ///  * sysbus MMIO region 0: device registers
    ///  * sysbus IRQ 0: `UARTINTR` (combined interrupt line)
    ///  * sysbus IRQ 1: `UARTRXINTR` (receive FIFO interrupt line)
    ///  * sysbus IRQ 2: `UARTTXINTR` (transmit FIFO interrupt line)
    ///  * sysbus IRQ 3: `UARTRTINTR` (receive timeout interrupt line)
    ///  * sysbus IRQ 4: `UARTMSINTR` (momem status interrupt line)
    ///  * sysbus IRQ 5: `UARTEINTR` (error interrupt line)
    /// ```
    #[doc(alias = "irq")]
    pub interrupts: [qemu_irq; 6usize],
    #[doc(alias = "clk")]
    pub clock: NonNull<Clock>,
    #[doc(alias = "migrate_clk")]
    pub migrate_clock: bool,
    /// The byte string that identifies the device.
    device_id: DeviceId,
}

impl ObjectImpl for PL011State {
    type Class = PL011Class;
    const TYPE_INFO: qemu_api::bindings::TypeInfo = qemu_api::type_info! { Self };
    const TYPE_NAME: &'static CStr = crate::TYPE_PL011;
    const PARENT_TYPE_NAME: Option<&'static CStr> = Some(TYPE_SYS_BUS_DEVICE);
    const ABSTRACT: bool = false;
    const INSTANCE_INIT: Option<unsafe extern "C" fn(obj: *mut Object)> = Some(pl011_init);
    const INSTANCE_POST_INIT: Option<unsafe extern "C" fn(obj: *mut Object)> = None;
    const INSTANCE_FINALIZE: Option<unsafe extern "C" fn(obj: *mut Object)> = None;
}

#[repr(C)]
pub struct PL011Class {
    _inner: [u8; 0],
}

impl qemu_api::definitions::Class for PL011Class {
    const CLASS_INIT: Option<unsafe extern "C" fn(klass: *mut ObjectClass, data: *mut c_void)> =
        Some(crate::device_class::pl011_class_init);
    const CLASS_BASE_INIT: Option<
        unsafe extern "C" fn(klass: *mut ObjectClass, data: *mut c_void),
    > = None;
}

impl PL011State {
    /// Initializes a pre-allocated, unitialized instance of `PL011State`.
    ///
    /// # Safety
    ///
    /// `self` must point to a correctly sized and aligned location for the
    /// `PL011State` type. It must not be called more than once on the same
    /// location/instance. All its fields are expected to hold unitialized
    /// values with the sole exception of `parent_obj`.
    unsafe fn init(&mut self) {
        const CLK_NAME: &CStr = c_str!("clk");

        let dev = addr_of_mut!(*self).cast::<DeviceState>();
        // SAFETY:
        //
        // self and self.iomem are guaranteed to be valid at this point since callers
        // must make sure the `self` reference is valid.
        unsafe {
            memory_region_init_io(
                addr_of_mut!(self.iomem),
                addr_of_mut!(*self).cast::<Object>(),
                &PL011_OPS,
                addr_of_mut!(*self).cast::<c_void>(),
                Self::TYPE_INFO.name,
                0x1000,
            );
            let sbd = addr_of_mut!(*self).cast::<SysBusDevice>();
            sysbus_init_mmio(sbd, addr_of_mut!(self.iomem));
            for irq in self.interrupts.iter_mut() {
                sysbus_init_irq(sbd, irq);
            }
        }
        // SAFETY:
        //
        // self.clock is not initialized at this point; but since `NonNull<_>` is Copy,
        // we can overwrite the undefined value without side effects. This is
        // safe since all PL011State instances are created by QOM code which
        // calls this function to initialize the fields; therefore no code is
        // able to access an invalid self.clock value.
        unsafe {
            self.clock = NonNull::new(qdev_init_clock_in(
                dev,
                CLK_NAME.as_ptr(),
                None, /* pl011_clock_update */
                addr_of_mut!(*self).cast::<c_void>(),
                ClockEvent::ClockUpdate.0,
            ))
            .unwrap();
        }
    }

    pub fn read(&mut self, offset: hwaddr, _size: c_uint) -> std::ops::ControlFlow<u64, u64> {
        use RegisterOffset::*;

        std::ops::ControlFlow::Break(match RegisterOffset::try_from(offset) {
            Err(v) if (0x3f8..0x400).contains(&v) => {
                u64::from(self.device_id[(offset - 0xfe0) >> 2])
            }
            Err(_) => {
                // qemu_log_mask(LOG_GUEST_ERROR, "pl011_read: Bad offset 0x%x\n", (int)offset);
                0
            }
            Ok(DR) => {
                self.flags.set_receive_fifo_full(false);
                let c = self.read_fifo[self.read_pos];
                if self.read_count > 0 {
                    self.read_count -= 1;
                    self.read_pos = (self.read_pos + 1) & (self.fifo_depth() - 1);
                }
                if self.read_count == 0 {
                    self.flags.set_receive_fifo_empty(true);
                }
                if self.read_count + 1 == self.read_trigger {
                    self.int_level &= !registers::INT_RX;
                }
                // Update error bits.
                self.receive_status_error_clear = c.to_be_bytes()[3].into();
                self.update();
                // Must call qemu_chr_fe_accept_input, so return Continue:
                return std::ops::ControlFlow::Continue(c.into());
            }
            Ok(RSR) => u8::from(self.receive_status_error_clear).into(),
            Ok(FR) => u16::from(self.flags).into(),
            Ok(FBRD) => self.fbrd.into(),
            Ok(ILPR) => self.ilpr.into(),
            Ok(IBRD) => self.ibrd.into(),
            Ok(LCR_H) => u16::from(self.line_control).into(),
            Ok(CR) => {
                // We exercise our self-control.
                u16::from(self.control).into()
            }
            Ok(FLS) => self.ifl.into(),
            Ok(IMSC) => self.int_enabled.into(),
            Ok(RIS) => self.int_level.into(),
            Ok(MIS) => u64::from(self.int_level & self.int_enabled),
            Ok(ICR) => {
                // "The UARTICR Register is the interrupt clear register and is write-only"
                // Source: ARM DDI 0183G 3.3.13 Interrupt Clear Register, UARTICR
                0
            }
            Ok(DMACR) => self.dmacr.into(),
        })
    }

    pub fn write(&mut self, offset: hwaddr, value: u64) {
        // eprintln!("write offset {offset} value {value}");
        use RegisterOffset::*;
        let value: u32 = value as u32;
        match RegisterOffset::try_from(offset) {
            Err(_bad_offset) => {
                eprintln!("write bad offset {offset} value {value}");
            }
            Ok(DR) => {
                // ??? Check if transmitter is enabled.
                let ch: u8 = value as u8;
                // XXX this blocks entire thread. Rewrite to use
                // qemu_chr_fe_write and background I/O callbacks

                // SAFETY: self.char_backend is a valid CharBackend instance after it's been
                // initialized in realize().
                unsafe {
                    qemu_chr_fe_write_all(addr_of_mut!(self.char_backend), &ch, 1);
                }
                self.loopback_tx(value);
                self.int_level |= registers::INT_TX;
                self.update();
            }
            Ok(RSR) => {
                self.receive_status_error_clear = 0.into();
            }
            Ok(FR) => {
                // flag writes are ignored
            }
            Ok(ILPR) => {
                self.ilpr = value;
            }
            Ok(IBRD) => {
                self.ibrd = value;
            }
            Ok(FBRD) => {
                self.fbrd = value;
            }
            Ok(LCR_H) => {
                let value = value as u16;
                let new_val: registers::LineControl = value.into();
                // Reset the FIFO state on FIFO enable or disable
                if bool::from(self.line_control.fifos_enabled())
                    ^ bool::from(new_val.fifos_enabled())
                {
                    self.reset_fifo();
                }
                if self.line_control.send_break() ^ new_val.send_break() {
                    let mut break_enable: c_int = new_val.send_break().into();
                    // SAFETY: self.char_backend is a valid CharBackend instance after it's been
                    // initialized in realize().
                    unsafe {
                        qemu_chr_fe_ioctl(
                            addr_of_mut!(self.char_backend),
                            CHR_IOCTL_SERIAL_SET_BREAK as i32,
                            addr_of_mut!(break_enable).cast::<c_void>(),
                        );
                    }
                    self.loopback_break(break_enable > 0);
                }
                self.line_control = new_val;
                self.set_read_trigger();
            }
            Ok(CR) => {
                // ??? Need to implement the enable bit.
                let value = value as u16;
                self.control = value.into();
                self.loopback_mdmctrl();
            }
            Ok(FLS) => {
                self.ifl = value;
                self.set_read_trigger();
            }
            Ok(IMSC) => {
                self.int_enabled = value;
                self.update();
            }
            Ok(RIS) => {}
            Ok(MIS) => {}
            Ok(ICR) => {
                self.int_level &= !value;
                self.update();
            }
            Ok(DMACR) => {
                self.dmacr = value;
                if value & 3 > 0 {
                    // qemu_log_mask(LOG_UNIMP, "pl011: DMA not implemented\n");
                    eprintln!("pl011: DMA not implemented");
                }
            }
        }
    }

    #[inline]
    fn loopback_tx(&mut self, value: u32) {
        if !self.loopback_enabled() {
            return;
        }

        // Caveat:
        //
        // In real hardware, TX loopback happens at the serial-bit level
        // and then reassembled by the RX logics back into bytes and placed
        // into the RX fifo. That is, loopback happens after TX fifo.
        //
        // Because the real hardware TX fifo is time-drained at the frame
        // rate governed by the configured serial format, some loopback
        // bytes in TX fifo may still be able to get into the RX fifo
        // that could be full at times while being drained at software
        // pace.
        //
        // In such scenario, the RX draining pace is the major factor
        // deciding which loopback bytes get into the RX fifo, unless
        // hardware flow-control is enabled.
        //
        // For simplicity, the above described is not emulated.
        self.put_fifo(value);
    }

    fn loopback_mdmctrl(&mut self) {
        if !self.loopback_enabled() {
            return;
        }

        /*
         * Loopback software-driven modem control outputs to modem status inputs:
         *   FR.RI  <= CR.Out2
         *   FR.DCD <= CR.Out1
         *   FR.CTS <= CR.RTS
         *   FR.DSR <= CR.DTR
         *
         * The loopback happens immediately even if this call is triggered
         * by setting only CR.LBE.
         *
         * CTS/RTS updates due to enabled hardware flow controls are not
         * dealt with here.
         */

        self.flags.set_ring_indicator(self.control.out_2());
        self.flags.set_data_carrier_detect(self.control.out_1());
        self.flags.set_clear_to_send(self.control.request_to_send());
        self.flags
            .set_data_set_ready(self.control.data_transmit_ready());

        // Change interrupts based on updated FR
        let mut il = self.int_level;

        il &= !Interrupt::MS;

        if self.flags.data_set_ready() {
            il |= Interrupt::DSR as u32;
        }
        if self.flags.data_carrier_detect() {
            il |= Interrupt::DCD as u32;
        }
        if self.flags.clear_to_send() {
            il |= Interrupt::CTS as u32;
        }
        if self.flags.ring_indicator() {
            il |= Interrupt::RI as u32;
        }
        self.int_level = il;
        self.update();
    }

    fn loopback_break(&mut self, enable: bool) {
        if enable {
            self.loopback_tx(DATA_BREAK);
        }
    }

    fn set_read_trigger(&mut self) {
        self.read_trigger = 1;
    }

    pub fn realize(&mut self) {
        // SAFETY: self.char_backend has the correct size and alignment for a
        // CharBackend object, and its callbacks are of the correct types.
        unsafe {
            qemu_chr_fe_set_handlers(
                addr_of_mut!(self.char_backend),
                Some(pl011_can_receive),
                Some(pl011_receive),
                Some(pl011_event),
                None,
                addr_of_mut!(*self).cast::<c_void>(),
                core::ptr::null_mut(),
                true,
            );
        }
    }

    pub fn reset(&mut self) {
        self.line_control.reset();
        self.receive_status_error_clear.reset();
        self.dmacr = 0;
        self.int_enabled = 0;
        self.int_level = 0;
        self.ilpr = 0;
        self.ibrd = 0;
        self.fbrd = 0;
        self.read_trigger = 1;
        self.ifl = 0x12;
        self.control.reset();
        self.flags = 0.into();
        self.reset_fifo();
    }

    pub fn reset_fifo(&mut self) {
        self.read_count = 0;
        self.read_pos = 0;

        /* Reset FIFO flags */
        self.flags.reset();
    }

    pub fn can_receive(&self) -> bool {
        // trace_pl011_can_receive(s->lcr, s->read_count, r);
        self.read_count < self.fifo_depth()
    }

    pub fn event(&mut self, event: QEMUChrEvent) {
        if event == bindings::QEMUChrEvent::CHR_EVENT_BREAK && !self.fifo_enabled() {
            self.put_fifo(DATA_BREAK);
            self.receive_status_error_clear.set_break_error(true);
        }
    }

    #[inline]
    pub fn fifo_enabled(&self) -> bool {
        matches!(self.line_control.fifos_enabled(), registers::Mode::FIFO)
    }

    #[inline]
    pub fn loopback_enabled(&self) -> bool {
        self.control.enable_loopback()
    }

    #[inline]
    pub fn fifo_depth(&self) -> usize {
        // Note: FIFO depth is expected to be power-of-2
        if self.fifo_enabled() {
            return PL011_FIFO_DEPTH;
        }
        1
    }

    pub fn put_fifo(&mut self, value: c_uint) {
        let depth = self.fifo_depth();
        assert!(depth > 0);
        let slot = (self.read_pos + self.read_count) & (depth - 1);
        self.read_fifo[slot] = value;
        self.read_count += 1;
        self.flags.set_receive_fifo_empty(false);
        if self.read_count == depth {
            self.flags.set_receive_fifo_full(true);
        }

        if self.read_count == self.read_trigger {
            self.int_level |= registers::INT_RX;
            self.update();
        }
    }

    pub fn update(&self) {
        let flags = self.int_level & self.int_enabled;
        for (irq, i) in self.interrupts.iter().zip(IRQMASK) {
            // SAFETY: self.interrupts have been initialized in init().
            unsafe { qemu_set_irq(*irq, i32::from(flags & i != 0)) };
        }
    }

    pub fn post_load(&mut self, _version_id: u32) -> Result<(), ()> {
        /* Sanity-check input state */
        if self.read_pos >= self.read_fifo.len() || self.read_count > self.read_fifo.len() {
            return Err(());
        }

        if !self.fifo_enabled() && self.read_count > 0 && self.read_pos > 0 {
            // Older versions of PL011 didn't ensure that the single
            // character in the FIFO in FIFO-disabled mode is in
            // element 0 of the array; convert to follow the current
            // code's assumptions.
            self.read_fifo[0] = self.read_fifo[self.read_pos];
            self.read_pos = 0;
        }

        self.ibrd &= IBRD_MASK;
        self.fbrd &= FBRD_MASK;

        Ok(())
    }
}

/// Which bits in the interrupt status matter for each outbound IRQ line ?
pub const IRQMASK: [u32; 6] = [
    /* combined IRQ */
    Interrupt::E
        | Interrupt::MS
        | Interrupt::RT as u32
        | Interrupt::TX as u32
        | Interrupt::RX as u32,
    Interrupt::RX as u32,
    Interrupt::TX as u32,
    Interrupt::RT as u32,
    Interrupt::MS,
    Interrupt::E,
];

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer, that has
/// the same size as [`PL011State`]. We also expect the device is
/// readable/writeable from one thread at any time.
pub unsafe extern "C" fn pl011_can_receive(opaque: *mut c_void) -> c_int {
    unsafe {
        debug_assert!(!opaque.is_null());
        let state = NonNull::new_unchecked(opaque.cast::<PL011State>());
        state.as_ref().can_receive().into()
    }
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer, that has
/// the same size as [`PL011State`]. We also expect the device is
/// readable/writeable from one thread at any time.
///
/// The buffer and size arguments must also be valid.
pub unsafe extern "C" fn pl011_receive(opaque: *mut c_void, buf: *const u8, size: c_int) {
    unsafe {
        debug_assert!(!opaque.is_null());
        let mut state = NonNull::new_unchecked(opaque.cast::<PL011State>());
        if state.as_ref().loopback_enabled() {
            return;
        }
        if size > 0 {
            debug_assert!(!buf.is_null());
            state.as_mut().put_fifo(c_uint::from(buf.read_volatile()))
        }
    }
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer, that has
/// the same size as [`PL011State`]. We also expect the device is
/// readable/writeable from one thread at any time.
pub unsafe extern "C" fn pl011_event(opaque: *mut c_void, event: QEMUChrEvent) {
    unsafe {
        debug_assert!(!opaque.is_null());
        let mut state = NonNull::new_unchecked(opaque.cast::<PL011State>());
        state.as_mut().event(event)
    }
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer for `chr`.
#[no_mangle]
pub unsafe extern "C" fn pl011_create(
    addr: u64,
    irq: qemu_irq,
    chr: *mut Chardev,
) -> *mut DeviceState {
    unsafe {
        let dev: *mut DeviceState = qdev_new(PL011State::TYPE_INFO.name);
        let sysbus: *mut SysBusDevice = dev.cast::<SysBusDevice>();

        qdev_prop_set_chr(dev, c_str!("chardev").as_ptr(), chr);
        sysbus_realize_and_unref(sysbus, addr_of!(error_fatal) as *mut *mut Error);
        sysbus_mmio_map(sysbus, 0, addr);
        sysbus_connect_irq(sysbus, 0, irq);
        dev
    }
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer, that has
/// the same size as [`PL011State`]. We also expect the device is
/// readable/writeable from one thread at any time.
pub unsafe extern "C" fn pl011_init(obj: *mut Object) {
    unsafe {
        debug_assert!(!obj.is_null());
        let mut state = NonNull::new_unchecked(obj.cast::<PL011State>());
        state.as_mut().init();
    }
}

#[repr(C)]
#[derive(Debug, qemu_api_macros::Object)]
/// PL011 Luminary device model.
pub struct PL011Luminary {
    parent_obj: PL011State,
}

#[repr(C)]
pub struct PL011LuminaryClass {
    _inner: [u8; 0],
}

/// Initializes a pre-allocated, unitialized instance of `PL011Luminary`.
///
/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer, that has
/// the same size as [`PL011Luminary`]. We also expect the device is
/// readable/writeable from one thread at any time.
pub unsafe extern "C" fn pl011_luminary_init(obj: *mut Object) {
    unsafe {
        debug_assert!(!obj.is_null());
        let mut state = NonNull::new_unchecked(obj.cast::<PL011Luminary>());
        let state = state.as_mut();
        state.parent_obj.device_id = DeviceId::Luminary;
    }
}

impl qemu_api::definitions::Class for PL011LuminaryClass {
    const CLASS_INIT: Option<unsafe extern "C" fn(klass: *mut ObjectClass, data: *mut c_void)> =
        None;
    const CLASS_BASE_INIT: Option<
        unsafe extern "C" fn(klass: *mut ObjectClass, data: *mut c_void),
    > = None;
}

impl ObjectImpl for PL011Luminary {
    type Class = PL011LuminaryClass;
    const TYPE_INFO: qemu_api::bindings::TypeInfo = qemu_api::type_info! { Self };
    const TYPE_NAME: &'static CStr = crate::TYPE_PL011_LUMINARY;
    const PARENT_TYPE_NAME: Option<&'static CStr> = Some(crate::TYPE_PL011);
    const ABSTRACT: bool = false;
    const INSTANCE_INIT: Option<unsafe extern "C" fn(obj: *mut Object)> = Some(pl011_luminary_init);
    const INSTANCE_POST_INIT: Option<unsafe extern "C" fn(obj: *mut Object)> = None;
    const INSTANCE_FINALIZE: Option<unsafe extern "C" fn(obj: *mut Object)> = None;
}
