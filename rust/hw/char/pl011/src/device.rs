// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use std::{ffi::CStr, mem::size_of, ptr::addr_of_mut};

use qemu_api::{
    chardev::{CharBackend, Chardev, Event},
    impl_vmstate_forward,
    irq::{IRQState, InterruptSource},
    memory::{hwaddr, MemoryRegion, MemoryRegionOps, MemoryRegionOpsBuilder},
    prelude::*,
    qdev::{Clock, ClockEvent, DeviceImpl, DeviceState, Property, ResetType, ResettablePhasesImpl},
    qom::{ObjectImpl, Owned, ParentField},
    static_assert,
    sysbus::{SysBusDevice, SysBusDeviceImpl},
    vmstate::VMStateDescription,
};

use crate::{
    device_class,
    registers::{self, Interrupt, RegisterOffset},
};

// TODO: You must disable the UART before any of the control registers are
// reprogrammed. When the UART is disabled in the middle of transmission or
// reception, it completes the current character before stopping

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
    pub clock: Owned<Clock>,
    #[doc(alias = "migrate_clk")]
    pub migrate_clock: bool,
}

// Some C users of this device embed its state struct into their own
// structs, so the size of the Rust version must not be any larger
// than the size of the C one. If this assert triggers you need to
// expand the padding_for_rust[] array in the C PL011State struct.
static_assert!(size_of::<PL011State>() <= size_of::<qemu_api::bindings::PL011State>());

qom_isa!(PL011State : SysBusDevice, DeviceState, Object);

#[repr(C)]
pub struct PL011Class {
    parent_class: <SysBusDevice as ObjectType>::Class,
    /// The byte string that identifies the device.
    device_id: DeviceId,
}

trait PL011Impl: SysBusDeviceImpl + IsA<PL011State> {
    const DEVICE_ID: DeviceId;
}

impl PL011Class {
    fn class_init<T: PL011Impl>(&mut self) {
        self.device_id = T::DEVICE_ID;
        self.parent_class.class_init::<T>();
    }
}

unsafe impl ObjectType for PL011State {
    type Class = PL011Class;
    const TYPE_NAME: &'static CStr = crate::TYPE_PL011;
}

impl PL011Impl for PL011State {
    const DEVICE_ID: DeviceId = DeviceId(&[0x11, 0x10, 0x14, 0x00, 0x0d, 0xf0, 0x05, 0xb1]);
}

impl ObjectImpl for PL011State {
    type ParentType = SysBusDevice;

    const INSTANCE_INIT: Option<unsafe fn(&mut Self)> = Some(Self::init);
    const INSTANCE_POST_INIT: Option<fn(&Self)> = Some(Self::post_init);
    const CLASS_INIT: fn(&mut Self::Class) = Self::Class::class_init::<Self>;
}

impl DeviceImpl for PL011State {
    fn properties() -> &'static [Property] {
        &device_class::PL011_PROPERTIES
    }
    fn vmsd() -> Option<&'static VMStateDescription> {
        Some(&device_class::VMSTATE_PL011)
    }
    const REALIZE: Option<fn(&Self)> = Some(Self::realize);
}

impl ResettablePhasesImpl for PL011State {
    const HOLD: Option<fn(&Self, ResetType)> = Some(Self::reset_hold);
}

impl SysBusDeviceImpl for PL011State {}

impl PL011Registers {
    pub(self) fn read(&mut self, offset: RegisterOffset) -> (bool, u32) {
        use RegisterOffset::*;

        let mut update = false;
        let result = match offset {
            DR => self.read_data_register(&mut update),
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
        char_backend: &CharBackend,
    ) -> bool {
        // eprintln!("write offset {offset} value {value}");
        use RegisterOffset::*;
        match offset {
            DR => return self.write_data_register(value),
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
                    let break_enable = new_val.send_break();
                    let _ = char_backend.send_break(break_enable);
                    self.loopback_break(break_enable)
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

    fn read_data_register(&mut self, update: &mut bool) -> u32 {
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
        self.receive_status_error_clear.set_from_data(c);
        *update = true;
        u32::from(c)
    }

    fn write_data_register(&mut self, value: u32) -> bool {
        // interrupts always checked
        let _ = self.loopback_tx(value.into());
        self.int_level |= Interrupt::TX.0;
        true
    }

    #[inline]
    #[must_use]
    fn loopback_tx(&mut self, value: registers::Data) -> bool {
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
        enable && self.loopback_tx(registers::Data::BREAK)
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
    pub fn put_fifo(&mut self, value: registers::Data) -> bool {
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
        static PL011_OPS: MemoryRegionOps<PL011State> = MemoryRegionOpsBuilder::<PL011State>::new()
            .read(&PL011State::read)
            .write(&PL011State::write)
            .native_endian()
            .impl_sizes(4, 4)
            .build();

        // SAFETY:
        //
        // self and self.iomem are guaranteed to be valid at this point since callers
        // must make sure the `self` reference is valid.
        MemoryRegion::init_io(
            unsafe { &mut *addr_of_mut!(self.iomem) },
            addr_of_mut!(*self),
            &PL011_OPS,
            "pl011",
            0x1000,
        );

        self.regs = Default::default();

        // SAFETY:
        //
        // self.clock is not initialized at this point; but since `Owned<_>` is
        // not Drop, we can overwrite the undefined value without side effects;
        // it's not sound but, because for all PL011State instances are created
        // by QOM code which calls this function to initialize the fields, at
        // leastno code is able to access an invalid self.clock value.
        self.clock = self.init_clock_in("clk", &Self::clock_update, ClockEvent::ClockUpdate);
    }

    const fn clock_update(&self, _event: ClockEvent) {
        /* pl011_trace_baudrate_change(s); */
    }

    fn post_init(&self) {
        self.init_mmio(&self.iomem);
        for irq in self.interrupts.iter() {
            self.init_irq(irq);
        }
    }

    fn read(&self, offset: hwaddr, _size: u32) -> u64 {
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
                    self.char_backend.accept_input();
                }
                result.into()
            }
        }
    }

    fn write(&self, offset: hwaddr, value: u64, _size: u32) {
        let mut update_irq = false;
        if let Ok(field) = RegisterOffset::try_from(offset) {
            // qemu_chr_fe_write_all() calls into the can_receive
            // callback, so handle writes before entering PL011Registers.
            if field == RegisterOffset::DR {
                // ??? Check if transmitter is enabled.
                let ch: [u8; 1] = [value as u8];
                // XXX this blocks entire thread. Rewrite to use
                // qemu_chr_fe_write and background I/O callbacks
                let _ = self.char_backend.write_all(&ch);
            }

            update_irq = self
                .regs
                .borrow_mut()
                .write(field, value as u32, &self.char_backend);
        } else {
            eprintln!("write bad offset {offset} value {value}");
        }
        if update_irq {
            self.update();
        }
    }

    fn can_receive(&self) -> u32 {
        let regs = self.regs.borrow();
        // trace_pl011_can_receive(s->lcr, s->read_count, r);
        u32::from(regs.read_count < regs.fifo_depth())
    }

    fn receive(&self, buf: &[u8]) {
        if buf.is_empty() {
            return;
        }
        let mut regs = self.regs.borrow_mut();
        let c: u32 = buf[0].into();
        let update_irq = !regs.loopback_enabled() && regs.put_fifo(c.into());
        // Release the BqlRefCell before calling self.update()
        drop(regs);

        if update_irq {
            self.update();
        }
    }

    fn event(&self, event: Event) {
        let mut update_irq = false;
        let mut regs = self.regs.borrow_mut();
        if event == Event::CHR_EVENT_BREAK && !regs.loopback_enabled() {
            update_irq = regs.put_fifo(registers::Data::BREAK);
        }
        // Release the BqlRefCell before calling self.update()
        drop(regs);

        if update_irq {
            self.update()
        }
    }

    fn realize(&self) {
        self.char_backend
            .enable_handlers(self, Self::can_receive, Self::receive, Self::event);
    }

    fn reset_hold(&self, _type: ResetType) {
        self.regs.borrow_mut().reset();
    }

    fn update(&self) {
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
/// We expect the FFI user of this function to pass a valid pointer for `chr`
/// and `irq`.
#[no_mangle]
pub unsafe extern "C" fn pl011_create(
    addr: u64,
    irq: *mut IRQState,
    chr: *mut Chardev,
) -> *mut DeviceState {
    // SAFETY: The callers promise that they have owned references.
    // They do not gift them to pl011_create, so use `Owned::from`.
    let irq = unsafe { Owned::<IRQState>::from(&*irq) };

    let dev = PL011State::new();
    if !chr.is_null() {
        let chr = unsafe { Owned::<Chardev>::from(&*chr) };
        dev.prop_set_chr("chardev", &chr);
    }
    dev.sysbus_realize();
    dev.mmio_map(0, addr);
    dev.connect_irq(0, &irq);

    // The pointer is kept alive by the QOM tree; drop the owned ref
    dev.as_mut_ptr()
}

#[repr(C)]
#[derive(qemu_api_macros::Object)]
/// PL011 Luminary device model.
pub struct PL011Luminary {
    parent_obj: ParentField<PL011State>,
}

qom_isa!(PL011Luminary : PL011State, SysBusDevice, DeviceState, Object);

unsafe impl ObjectType for PL011Luminary {
    type Class = <PL011State as ObjectType>::Class;
    const TYPE_NAME: &'static CStr = crate::TYPE_PL011_LUMINARY;
}

impl ObjectImpl for PL011Luminary {
    type ParentType = PL011State;

    const CLASS_INIT: fn(&mut Self::Class) = Self::Class::class_init::<Self>;
}

impl PL011Impl for PL011Luminary {
    const DEVICE_ID: DeviceId = DeviceId(&[0x11, 0x00, 0x18, 0x01, 0x0d, 0xf0, 0x05, 0xb1]);
}

impl DeviceImpl for PL011Luminary {}
impl ResettablePhasesImpl for PL011Luminary {}
impl SysBusDeviceImpl for PL011Luminary {}
