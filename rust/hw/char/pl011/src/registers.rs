// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

//! Device registers exposed as typed structs which are backed by arbitrary
//! integer bitmaps. [`Data`], [`Control`], [`LineControl`], etc.

// For more detail see the PL011 Technical Reference Manual DDI0183:
// https://developer.arm.com/documentation/ddi0183/latest/

use bilge::prelude::*;
use bits::bits;
use migration::{impl_vmstate_bitsized, impl_vmstate_forward};

/// Offset of each register from the base memory address of the device.
#[doc(alias = "offset")]
#[allow(non_camel_case_types)]
#[repr(u64)]
#[derive(Debug, Eq, PartialEq, common::TryInto)]
pub enum RegisterOffset {
    /// Data Register
    ///
    /// A write to this register initiates the actual data transmission
    #[doc(alias = "UARTDR")]
    DR = 0x000,
    /// Receive Status Register or Error Clear Register
    #[doc(alias = "UARTRSR")]
    #[doc(alias = "UARTECR")]
    RSR = 0x004,
    /// Flag Register
    ///
    /// A read of this register shows if transmission is complete
    #[doc(alias = "UARTFR")]
    FR = 0x018,
    /// Fractional Baud Rate Register
    ///
    /// responsible for baud rate speed
    #[doc(alias = "UARTFBRD")]
    FBRD = 0x028,
    /// `IrDA` Low-Power Counter Register
    #[doc(alias = "UARTILPR")]
    ILPR = 0x020,
    /// Integer Baud Rate Register
    ///
    /// Responsible for baud rate speed
    #[doc(alias = "UARTIBRD")]
    IBRD = 0x024,
    /// line control register (data frame format)
    #[doc(alias = "UARTLCR_H")]
    LCR_H = 0x02C,
    /// Toggle UART, transmission or reception
    #[doc(alias = "UARTCR")]
    CR = 0x030,
    /// Interrupt FIFO Level Select Register
    #[doc(alias = "UARTIFLS")]
    FLS = 0x034,
    /// Interrupt Mask Set/Clear Register
    #[doc(alias = "UARTIMSC")]
    IMSC = 0x038,
    /// Raw Interrupt Status Register
    #[doc(alias = "UARTRIS")]
    RIS = 0x03C,
    /// Masked Interrupt Status Register
    #[doc(alias = "UARTMIS")]
    MIS = 0x040,
    /// Interrupt Clear Register
    #[doc(alias = "UARTICR")]
    ICR = 0x044,
    /// DMA control Register
    #[doc(alias = "UARTDMACR")]
    DMACR = 0x048,
    ///// Reserved, offsets `0x04C` to `0x07C`.
    //Reserved = 0x04C,
}

/// Receive Status Register / Data Register common error bits
///
/// The `UARTRSR` register is updated only when a read occurs
/// from the `UARTDR` register with the same status information
/// that can also be obtained by reading the `UARTDR` register
#[bitsize(8)]
#[derive(Clone, Copy, Default, DebugBits, FromBits)]
pub struct Errors {
    pub framing_error: bool,
    pub parity_error: bool,
    pub break_error: bool,
    pub overrun_error: bool,
    _reserved_unpredictable: u4,
}

/// Data Register, `UARTDR`
///
/// The `UARTDR` register is the data register; write for TX and
/// read for RX. It is a 12-bit register, where bits 7..0 are the
/// character and bits 11..8 are error bits.
#[bitsize(32)]
#[derive(Clone, Copy, Default, DebugBits, FromBits)]
#[doc(alias = "UARTDR")]
pub struct Data {
    pub data: u8,
    pub errors: Errors,
    _reserved: u16,
}
impl_vmstate_bitsized!(Data);

impl Data {
    // bilge is not very const-friendly, unfortunately
    pub const BREAK: Self = Self { value: 1 << 10 };
}

/// Receive Status Register / Error Clear Register, `UARTRSR/UARTECR`
///
/// This register provides a different way to read the four receive
/// status error bits that can be found in bits 11..8 of the UARTDR
/// on a read. It gets updated when the guest reads UARTDR, and the
/// status bits correspond to that character that was just read.
///
/// The TRM confusingly describes this offset as UARTRSR for reads
/// and UARTECR for writes, but really it's a single error status
/// register where writing anything to the register clears the error
/// bits.
#[bitsize(32)]
#[derive(Clone, Copy, DebugBits, FromBits)]
pub struct ReceiveStatusErrorClear {
    pub errors: Errors,
    _reserved_unpredictable: u24,
}
impl_vmstate_bitsized!(ReceiveStatusErrorClear);

impl ReceiveStatusErrorClear {
    pub fn set_from_data(&mut self, data: Data) {
        self.set_errors(data.errors());
    }

    pub fn reset(&mut self) {
        // All the bits are cleared to 0 on reset.
        *self = Self::default();
    }
}

impl Default for ReceiveStatusErrorClear {
    fn default() -> Self {
        0.into()
    }
}

#[bitsize(32)]
#[derive(Clone, Copy, DebugBits, FromBits)]
/// Flag Register, `UARTFR`
///
/// This has the usual inbound RS232 modem-control signals, plus flags
/// for RX and TX FIFO fill levels and a BUSY flag.
#[doc(alias = "UARTFR")]
pub struct Flags {
    /// CTS: Clear to send
    pub clear_to_send: bool,
    /// DSR: Data set ready
    pub data_set_ready: bool,
    /// DCD: Data carrier detect
    pub data_carrier_detect: bool,
    /// BUSY: UART busy. In real hardware, set while the UART is
    /// busy transmitting data. QEMU's implementation never sets BUSY.
    pub busy: bool,
    /// RXFE: Receive FIFO empty
    pub receive_fifo_empty: bool,
    /// TXFF: Transmit FIFO full
    pub transmit_fifo_full: bool,
    /// RXFF: Receive FIFO full
    pub receive_fifo_full: bool,
    /// TXFE: Transmit FIFO empty
    pub transmit_fifo_empty: bool,
    /// RI: Ring indicator
    pub ring_indicator: bool,
    _reserved_zero_no_modify: u23,
}
impl_vmstate_bitsized!(Flags);

impl Flags {
    pub fn reset(&mut self) {
        *self = Self::default();
    }
}

impl Default for Flags {
    fn default() -> Self {
        let mut ret: Self = 0.into();
        // After reset TXFF, RXFF, and BUSY are 0, and TXFE and RXFE are 1
        ret.set_receive_fifo_empty(true);
        ret.set_transmit_fifo_empty(true);
        ret
    }
}

#[bitsize(32)]
#[derive(Clone, Copy, DebugBits, FromBits)]
/// Line Control Register, `UARTLCR_H`
#[doc(alias = "UARTLCR_H")]
pub struct LineControl {
    /// BRK: Send break
    pub send_break: bool,
    /// PEN: Parity enable
    pub parity_enabled: bool,
    /// EPS: Even parity select
    pub parity: Parity,
    /// STP2: Two stop bits select
    pub two_stops_bits: bool,
    /// FEN: Enable FIFOs
    pub fifos_enabled: Mode,
    /// WLEN: Word length in bits
    /// b11 = 8 bits
    /// b10 = 7 bits
    /// b01 = 6 bits
    /// b00 = 5 bits.
    pub word_length: WordLength,
    /// SPS Stick parity select
    pub sticky_parity: bool,
    /// 31:8 - Reserved, do not modify, read as zero.
    _reserved_zero_no_modify: u24,
}
impl_vmstate_bitsized!(LineControl);

impl LineControl {
    pub fn reset(&mut self) {
        // All the bits are cleared to 0 when reset.
        *self = 0.into();
    }
}

impl Default for LineControl {
    fn default() -> Self {
        0.into()
    }
}

#[bitsize(1)]
#[derive(Clone, Copy, Debug, Eq, FromBits, PartialEq)]
/// `EPS` "Even parity select", field of [Line Control
/// register](LineControl).
pub enum Parity {
    Odd = 0,
    Even = 1,
}

#[bitsize(1)]
#[derive(Clone, Copy, Debug, Eq, FromBits, PartialEq)]
/// `FEN` "Enable FIFOs" or Device mode, field of [Line Control
/// register](LineControl).
pub enum Mode {
    /// 0 = FIFOs are disabled (character mode) that is, the FIFOs become
    /// 1-byte-deep holding registers
    Character = 0,
    /// 1 = transmit and receive FIFO buffers are enabled (FIFO mode).
    FIFO = 1,
}

#[bitsize(2)]
#[derive(Clone, Copy, Debug, Eq, FromBits, PartialEq)]
#[allow(clippy::enum_variant_names)]
/// `WLEN` Word length, field of [Line Control register](LineControl).
///
/// These bits indicate the number of data bits transmitted or received in a
/// frame as follows:
pub enum WordLength {
    /// b11 = 8 bits
    _8Bits = 0b11,
    /// b10 = 7 bits
    _7Bits = 0b10,
    /// b01 = 6 bits
    _6Bits = 0b01,
    /// b00 = 5 bits.
    _5Bits = 0b00,
}

/// Control Register, `UARTCR`
///
/// The `UARTCR` register is the control register. It contains various
/// enable bits, and the bits to write to set the usual outbound RS232
/// modem control signals. All bits reset to 0 except TXE and RXE.
#[bitsize(32)]
#[doc(alias = "UARTCR")]
#[derive(Clone, Copy, DebugBits, FromBits)]
pub struct Control {
    /// `UARTEN` UART enable: 0 = UART is disabled.
    pub enable_uart: bool,
    /// `SIREN` `SIR` enable: disable or enable IrDA SIR ENDEC.
    /// QEMU does not model this.
    pub enable_sir: bool,
    /// `SIRLP` SIR low-power IrDA mode. QEMU does not model this.
    pub sir_lowpower_irda_mode: u1,
    /// Reserved, do not modify, read as zero.
    _reserved_zero_no_modify: u4,
    /// `LBE` Loopback enable: feed UART output back to the input
    pub enable_loopback: bool,
    /// `TXE` Transmit enable
    pub enable_transmit: bool,
    /// `RXE` Receive enable
    pub enable_receive: bool,
    /// `DTR` Data transmit ready
    pub data_transmit_ready: bool,
    /// `RTS` Request to send
    pub request_to_send: bool,
    /// `Out1` UART Out1 signal; can be used as DCD
    pub out_1: bool,
    /// `Out2` UART Out2 signal; can be used as RI
    pub out_2: bool,
    /// `RTSEn` RTS hardware flow control enable
    pub rts_hardware_flow_control_enable: bool,
    /// `CTSEn` CTS hardware flow control enable
    pub cts_hardware_flow_control_enable: bool,
    /// 31:16 - Reserved, do not modify, read as zero.
    _reserved_zero_no_modify2: u16,
}
impl_vmstate_bitsized!(Control);

impl Control {
    pub fn reset(&mut self) {
        *self = 0.into();
        self.set_enable_receive(true);
        self.set_enable_transmit(true);
    }
}

impl Default for Control {
    fn default() -> Self {
        let mut ret: Self = 0.into();
        ret.reset();
        ret
    }
}

bits! {
    /// Interrupt status bits in UARTRIS, UARTMIS, UARTIMSC
    #[derive(Default)]
    pub struct Interrupt(u32) {
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
impl_vmstate_forward!(Interrupt);
