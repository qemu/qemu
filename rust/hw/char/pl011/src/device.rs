// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use core::ptr::{addr_of, addr_of_mut, NonNull};
use std::{
    ffi::CStr,
    os::raw::{c_int, c_void},
};

use qemu_api::{
    bindings::{
        error_fatal, hwaddr, memory_region_init_io, qdev_init_clock_in, qdev_new,
        qdev_prop_set_chr, qemu_chr_fe_accept_input, qemu_chr_fe_ioctl, qemu_chr_fe_set_handlers,
        qemu_chr_fe_write_all, qemu_irq, sysbus_connect_irq, sysbus_mmio_map,
        sysbus_realize_and_unref, CharBackend, Chardev, Clock, ClockEvent, MemoryRegion,
        QEMUChrEvent, CHR_IOCTL_SERIAL_SET_BREAK,
    },
    c_str, impl_vmstate_forward,
    irq::InterruptSource,
    prelude::*,
    qdev::{DeviceImpl, DeviceState, Property},
    qom::{ClassInitImpl, ObjectImpl, ParentField},
    sysbus::{SysBusDevice, SysBusDeviceClass},
    vmstate::VMStateDescription,
};

use crate::{
    device_class,
    memory_ops::PL011_OPS,
    registers::{self, Interrupt},
    RegisterOffset,
};

/// Integer Baud Rate Divider, `UARTIBRD`
const IBRD_MASK: u32 = 0xffff;

/// Fractional Baud Rate Divider, `UARTFBRD`
const FBRD_MASK: u32 = 0x3f;

/// QEMU sourced constant.
pub const PL011_FIFO_DEPTH: u32 = 16;

#[derive(Clone, Copy)]
struct DeviceId(&'static [u8; 8]);

impl std::ops::Index<hwaddr> for DeviceId {
    type Output = u8;

    fn index(&self, idx: hwaddr) -> &Self::Output {
        &self.0[idx as usize]
    }
}

impl DeviceId {
    const ARM: Self = Self(&[0x11, 0x10, 0x14, 0x00, 0x0d, 0xf0, 0x05, 0xb1]);
    const LUMINARY: Self = Self(&[0x11, 0x00, 0x18, 0x01, 0x0d, 0xf0, 0x05, 0xb1]);
}

// FIFOs use 32-bit indices instead of usize, for compatibility with
// the migration stream produced by the C version of this device.
#[repr(transparent)]
#[derive(Debug, Default)]
pub struct Fifo([registers::Data; PL011_FIFO_DEPTH as usize]);
impl_vmstate_forward!(Fifo);

impl Fifo {
    const fn len(&self) -> u32 {
        self.0.len() as u32
    }
}

impl std::ops::IndexMut<u32> for Fifo {
    fn index_mut(&mut self, idx: u32) -> &mut Self::Output {
        &mut self.0[idx as usize]
    }
}

impl std::ops::Index<u32> for Fifo {
    type Output = registers::Data;

    fn index(&self, idx: u32) -> &Self::Output {
        &self.0[idx as usize]
    }
}

#[repr(C)]
#[derive(Debug, Default, qemu_api_macros::offsets)]
pub struct PL011Registers {
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
    pub read_fifo: Fifo,
    pub ilpr: u32,
    pub ibrd: u32,
    pub fbrd: u32,
    pub ifl: u32,
    pub read_pos: u32,
    pub read_count: u32,
    pub read_trigger: u32,
}

#[repr(C)]
#[derive(qemu_api_macros::Object, qemu_api_macros::offsets)]
/// PL011 Device Model in QEMU
pub struct PL011State {
    pub parent_obj: ParentField<SysBusDevice>,
    pub iomem: MemoryRegion,
    #[doc(alias = "chr")]
    pub char_backend: CharBackend,
    pub regs: BqlRefCell<PL011Registers>,
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
    pub interrupts: [InterruptSource; IRQMASK.len()],
    #[doc(alias = "clk")]
    pub clock: NonNull<Clock>,
    #[doc(alias = "migrate_clk")]
    pub migrate_clock: bool,
}

qom_isa!(PL011State : SysBusDevice, DeviceState, Object);

#[repr(C)]
pub struct PL011Class {
    parent_class: <SysBusDevice as ObjectType>::Class,
    /// The byte string that identifies the device.
    device_id: DeviceId,
}

unsafe impl ObjectType for PL011State {
    type Class = PL011Class;
    const TYPE_NAME: &'static CStr = crate::TYPE_PL011;
}

impl ClassInitImpl<PL011Class> for PL011State {
    fn class_init(klass: &mut PL011Class) {
        klass.device_id = DeviceId::ARM;
        <Self as ClassInitImpl<SysBusDeviceClass>>::class_init(&mut klass.parent_class);
    }
}

impl ObjectImpl for PL011State {
    type ParentType = SysBusDevice;

    const INSTANCE_INIT: Option<unsafe fn(&mut Self)> = Some(Self::init);
    const INSTANCE_POST_INIT: Option<fn(&Self)> = Some(Self::post_init);
}

impl DeviceImpl for PL011State {
    fn properties() -> &'static [Property] {
        &device_class::PL011_PROPERTIES
    }
    fn vmsd() -> Option<&'static VMStateDescription> {
        Some(&device_class::VMSTATE_PL011)
    }
    const REALIZE: Option<fn(&Self)> = Some(Self::realize);
    const RESET: Option<fn(&Self)> = Some(Self::reset);
}

impl PL011Registers {
    pub(self) fn read(&mut self, offset: RegisterOffset) -> (bool, u32) {
        use RegisterOffset::*;

        let mut update = false;
        let result = match offset {
            DR => {
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
                    self.int_level &= !Interrupt::RX.0;
                }
                // Update error bits.
                self.receive_status_error_clear.set_from_data(c);
                // Must call qemu_chr_fe_accept_input
                update = true;
                u32::from(c)
            }
            RSR => u32::from(self.receive_status_error_clear),
            FR => u32::from(self.flags),
            FBRD => self.fbrd,
            ILPR => self.ilpr,
            IBRD => self.ibrd,
            LCR_H => u32::from(self.line_control),
            CR => u32::from(self.control),
            FLS => self.ifl,
            IMSC => self.int_enabled,
            RIS => self.int_level,
            MIS => self.int_level & self.int_enabled,
            ICR => {
                // "The UARTICR Register is the interrupt clear register and is write-only"
                // Source: ARM DDI 0183G 3.3.13 Interrupt Clear Register, UARTICR
                0
            }
            DMACR => self.dmacr,
        };
        (update, result)
    }

    pub(self) fn write(
        &mut self,
        offset: RegisterOffset,
        value: u32,
        char_backend: *mut CharBackend,
    ) -> bool {
        // eprintln!("write offset {offset} value {value}");
        use RegisterOffset::*;
        match offset {
            DR => {
                // interrupts always checked
                let _ = self.loopback_tx(value);
                self.int_level |= Interrupt::TX.0;
                return true;
            }
            RSR => {
                self.receive_status_error_clear = 0.into();
            }
            FR => {
                // flag writes are ignored
            }
            ILPR => {
                self.ilpr = value;
            }
            IBRD => {
                self.ibrd = value;
            }
            FBRD => {
                self.fbrd = value;
            }
            LCR_H => {
                let new_val: registers::LineControl = value.into();
                // Reset the FIFO state on FIFO enable or disable
                if self.line_control.fifos_enabled() != new_val.fifos_enabled() {
                    self.reset_rx_fifo();
                    self.reset_tx_fifo();
                }
                let update = (self.line_control.send_break() != new_val.send_break()) && {
                    let mut break_enable: c_int = new_val.send_break().into();
                    // SAFETY: self.char_backend is a valid CharBackend instance after it's been
                    // initialized in realize().
                    unsafe {
                        qemu_chr_fe_ioctl(
                            char_backend,
                            CHR_IOCTL_SERIAL_SET_BREAK as i32,
                            addr_of_mut!(break_enable).cast::<c_void>(),
                        );
                    }
                    self.loopback_break(break_enable > 0)
                };
                self.line_control = new_val;
                self.set_read_trigger();
                return update;
            }
            CR => {
                // ??? Need to implement the enable bit.
                self.control = value.into();
                return self.loopback_mdmctrl();
            }
            FLS => {
                self.ifl = value;
                self.set_read_trigger();
            }
            IMSC => {
                self.int_enabled = value;
                return true;
            }
            RIS => {}
            MIS => {}
            ICR => {
                self.int_level &= !value;
                return true;
            }
            DMACR => {
                self.dmacr = value;
                if value & 3 > 0 {
                    // qemu_log_mask(LOG_UNIMP, "pl011: DMA not implemented\n");
                    eprintln!("pl011: DMA not implemented");
                }
            }
        }
        false
    }

    #[inline]
    #[must_use]
    fn loopback_tx(&mut self, value: u32) -> bool {
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
        self.loopback_enabled() && self.put_fifo(value)
    }

    #[must_use]
    fn loopback_mdmctrl(&mut self) -> bool {
        if !self.loopback_enabled() {
            return false;
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

        il &= !Interrupt::MS.0;

        if self.flags.data_set_ready() {
            il |= Interrupt::DSR.0;
        }
        if self.flags.data_carrier_detect() {
            il |= Interrupt::DCD.0;
        }
        if self.flags.clear_to_send() {
            il |= Interrupt::CTS.0;
        }
        if self.flags.ring_indicator() {
            il |= Interrupt::RI.0;
        }
        self.int_level = il;
        true
    }

    fn loopback_break(&mut self, enable: bool) -> bool {
        enable && self.loopback_tx(registers::Data::BREAK.into())
    }

    fn set_read_trigger(&mut self) {
        self.read_trigger = 1;
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
        self.flags.reset();
        self.reset_rx_fifo();
        self.reset_tx_fifo();
    }

    pub fn reset_rx_fifo(&mut self) {
        self.read_count = 0;
        self.read_pos = 0;

        // Reset FIFO flags
        self.flags.set_receive_fifo_full(false);
        self.flags.set_receive_fifo_empty(true);
    }

    pub fn reset_tx_fifo(&mut self) {
        // Reset FIFO flags
        self.flags.set_transmit_fifo_full(false);
        self.flags.set_transmit_fifo_empty(true);
    }

    #[inline]
    pub fn fifo_enabled(&self) -> bool {
        self.line_control.fifos_enabled() == registers::Mode::FIFO
    }

    #[inline]
    pub fn loopback_enabled(&self) -> bool {
        self.control.enable_loopback()
    }

    #[inline]
    pub fn fifo_depth(&self) -> u32 {
        // Note: FIFO depth is expected to be power-of-2
        if self.fifo_enabled() {
            return PL011_FIFO_DEPTH;
        }
        1
    }

    #[must_use]
    pub fn put_fifo(&mut self, value: u32) -> bool {
        let depth = self.fifo_depth();
        assert!(depth > 0);
        let slot = (self.read_pos + self.read_count) & (depth - 1);
        self.read_fifo[slot] = registers::Data::from(value);
        self.read_count += 1;
        self.flags.set_receive_fifo_empty(false);
        if self.read_count == depth {
            self.flags.set_receive_fifo_full(true);
        }

        if self.read_count == self.read_trigger {
            self.int_level |= Interrupt::RX.0;
            return true;
        }
        false
    }

    pub fn post_load(&mut self) -> Result<(), ()> {
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
                Self::TYPE_NAME.as_ptr(),
                0x1000,
            );
        }

        self.regs = Default::default();

        // SAFETY:
        //
        // self.clock is not initialized at this point; but since `NonNull<_>` is Copy,
        // we can overwrite the undefined value without side effects. This is
        // safe since all PL011State instances are created by QOM code which
        // calls this function to initialize the fields; therefore no code is
        // able to access an invalid self.clock value.
        unsafe {
            let dev: &mut DeviceState = self.upcast_mut();
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

    fn post_init(&self) {
        self.init_mmio(&self.iomem);
        for irq in self.interrupts.iter() {
            self.init_irq(irq);
        }
    }

    pub fn read(&mut self, offset: hwaddr, _size: u32) -> u64 {
        match RegisterOffset::try_from(offset) {
            Err(v) if (0x3f8..0x400).contains(&(v >> 2)) => {
                let device_id = self.get_class().device_id;
                u64::from(device_id[(offset - 0xfe0) >> 2])
            }
            Err(_) => {
                // qemu_log_mask(LOG_GUEST_ERROR, "pl011_read: Bad offset 0x%x\n", (int)offset);
                0
            }
            Ok(field) => {
                let (update_irq, result) = self.regs.borrow_mut().read(field);
                if update_irq {
                    self.update();
                    unsafe {
                        qemu_chr_fe_accept_input(&mut self.char_backend);
                    }
                }
                result.into()
            }
        }
    }

    pub fn write(&mut self, offset: hwaddr, value: u64) {
        let mut update_irq = false;
        if let Ok(field) = RegisterOffset::try_from(offset) {
            // qemu_chr_fe_write_all() calls into the can_receive
            // callback, so handle writes before entering PL011Registers.
            if field == RegisterOffset::DR {
                // ??? Check if transmitter is enabled.
                let ch: u8 = value as u8;
                // SAFETY: char_backend is a valid CharBackend instance after it's been
                // initialized in realize().
                // XXX this blocks entire thread. Rewrite to use
                // qemu_chr_fe_write and background I/O callbacks
                unsafe {
                    qemu_chr_fe_write_all(&mut self.char_backend, &ch, 1);
                }
            }

            update_irq = self
                .regs
                .borrow_mut()
                .write(field, value as u32, &mut self.char_backend);
        } else {
            eprintln!("write bad offset {offset} value {value}");
        }
        if update_irq {
            self.update();
        }
    }

    pub fn can_receive(&self) -> bool {
        // trace_pl011_can_receive(s->lcr, s->read_count, r);
        let regs = self.regs.borrow();
        regs.read_count < regs.fifo_depth()
    }

    pub fn receive(&self, ch: u32) {
        let mut regs = self.regs.borrow_mut();
        let update_irq = !regs.loopback_enabled() && regs.put_fifo(ch);
        // Release the BqlRefCell before calling self.update()
        drop(regs);

        if update_irq {
            self.update();
        }
    }

    pub fn event(&self, event: QEMUChrEvent) {
        let mut update_irq = false;
        let mut regs = self.regs.borrow_mut();
        if event == QEMUChrEvent::CHR_EVENT_BREAK && !regs.loopback_enabled() {
            update_irq = regs.put_fifo(registers::Data::BREAK.into());
        }
        // Release the BqlRefCell before calling self.update()
        drop(regs);

        if update_irq {
            self.update()
        }
    }

    pub fn realize(&self) {
        // SAFETY: self.char_backend has the correct size and alignment for a
        // CharBackend object, and its callbacks are of the correct types.
        unsafe {
            qemu_chr_fe_set_handlers(
                addr_of!(self.char_backend) as *mut CharBackend,
                Some(pl011_can_receive),
                Some(pl011_receive),
                Some(pl011_event),
                None,
                addr_of!(*self).cast::<c_void>() as *mut c_void,
                core::ptr::null_mut(),
                true,
            );
        }
    }

    pub fn reset(&self) {
        self.regs.borrow_mut().reset();
    }

    pub fn update(&self) {
        let regs = self.regs.borrow();
        let flags = regs.int_level & regs.int_enabled;
        for (irq, i) in self.interrupts.iter().zip(IRQMASK) {
            irq.set(flags & i != 0);
        }
    }

    pub fn post_load(&self, _version_id: u32) -> Result<(), ()> {
        self.regs.borrow_mut().post_load()
    }
}

/// Which bits in the interrupt status matter for each outbound IRQ line ?
const IRQMASK: [u32; 6] = [
    /* combined IRQ */
    Interrupt::E.0 | Interrupt::MS.0 | Interrupt::RT.0 | Interrupt::TX.0 | Interrupt::RX.0,
    Interrupt::RX.0,
    Interrupt::TX.0,
    Interrupt::RT.0,
    Interrupt::MS.0,
    Interrupt::E.0,
];

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer, that has
/// the same size as [`PL011State`]. We also expect the device is
/// readable/writeable from one thread at any time.
pub unsafe extern "C" fn pl011_can_receive(opaque: *mut c_void) -> c_int {
    let state = NonNull::new(opaque).unwrap().cast::<PL011State>();
    unsafe { state.as_ref().can_receive().into() }
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer, that has
/// the same size as [`PL011State`]. We also expect the device is
/// readable/writeable from one thread at any time.
///
/// The buffer and size arguments must also be valid.
pub unsafe extern "C" fn pl011_receive(opaque: *mut c_void, buf: *const u8, size: c_int) {
    let state = NonNull::new(opaque).unwrap().cast::<PL011State>();
    unsafe {
        if size > 0 {
            debug_assert!(!buf.is_null());
            state.as_ref().receive(u32::from(buf.read_volatile()));
        }
    }
}

/// # Safety
///
/// We expect the FFI user of this function to pass a valid pointer, that has
/// the same size as [`PL011State`]. We also expect the device is
/// readable/writeable from one thread at any time.
pub unsafe extern "C" fn pl011_event(opaque: *mut c_void, event: QEMUChrEvent) {
    let state = NonNull::new(opaque).unwrap().cast::<PL011State>();
    unsafe { state.as_ref().event(event) }
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
        let dev: *mut DeviceState = qdev_new(PL011State::TYPE_NAME.as_ptr());
        let sysbus: *mut SysBusDevice = dev.cast::<SysBusDevice>();

        qdev_prop_set_chr(dev, c_str!("chardev").as_ptr(), chr);
        sysbus_realize_and_unref(sysbus, addr_of_mut!(error_fatal));
        sysbus_mmio_map(sysbus, 0, addr);
        sysbus_connect_irq(sysbus, 0, irq);
        dev
    }
}

#[repr(C)]
#[derive(qemu_api_macros::Object)]
/// PL011 Luminary device model.
pub struct PL011Luminary {
    parent_obj: ParentField<PL011State>,
}

impl ClassInitImpl<PL011Class> for PL011Luminary {
    fn class_init(klass: &mut PL011Class) {
        klass.device_id = DeviceId::LUMINARY;
        <Self as ClassInitImpl<SysBusDeviceClass>>::class_init(&mut klass.parent_class);
    }
}

qom_isa!(PL011Luminary : PL011State, SysBusDevice, DeviceState, Object);

unsafe impl ObjectType for PL011Luminary {
    type Class = <PL011State as ObjectType>::Class;
    const TYPE_NAME: &'static CStr = crate::TYPE_PL011_LUMINARY;
}

impl ObjectImpl for PL011Luminary {
    type ParentType = PL011State;
}

impl DeviceImpl for PL011Luminary {}
