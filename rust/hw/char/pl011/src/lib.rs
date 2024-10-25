// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// PL011 QEMU Device Model
//
// This library implements a device model for the PrimeCell® UART (PL011)
// device in QEMU.
//
#![doc = include_str!("../README.md")]
//! # Library crate
//!
//! See [`PL011State`](crate::device::PL011State) for the device model type and
//! the [`registers`] module for register types.

#![deny(
    rustdoc::broken_intra_doc_links,
    rustdoc::redundant_explicit_links,
    clippy::correctness,
    clippy::suspicious,
    clippy::complexity,
    clippy::perf,
    clippy::cargo,
    clippy::nursery,
    clippy::style,
    // restriction group
    clippy::dbg_macro,
    clippy::as_underscore,
    clippy::assertions_on_result_states,
    // pedantic group
    clippy::doc_markdown,
    clippy::borrow_as_ptr,
    clippy::cast_lossless,
    clippy::option_if_let_else,
    clippy::missing_const_for_fn,
    clippy::cognitive_complexity,
    clippy::missing_safety_doc,
    )]
#![allow(clippy::result_unit_err)]

extern crate bilge;
extern crate bilge_impl;
extern crate qemu_api;

use qemu_api::c_str;

pub mod device;
pub mod device_class;
pub mod memory_ops;

pub const TYPE_PL011: &::std::ffi::CStr = c_str!("pl011");
pub const TYPE_PL011_LUMINARY: &::std::ffi::CStr = c_str!("pl011_luminary");

/// Offset of each register from the base memory address of the device.
///
/// # Source
/// ARM DDI 0183G, Table 3-1 p.3-3
#[doc(alias = "offset")]
#[allow(non_camel_case_types)]
#[repr(u64)]
#[derive(Debug)]
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

impl core::convert::TryFrom<u64> for RegisterOffset {
    type Error = u64;

    fn try_from(value: u64) -> Result<Self, Self::Error> {
        macro_rules! case {
            ($($discriminant:ident),*$(,)*) => {
                /* check that matching on all macro arguments compiles, which means we are not
                 * missing any enum value; if the type definition ever changes this will stop
                 * compiling.
                 */
                const fn _assert_exhaustive(val: RegisterOffset) {
                    match val {
                        $(RegisterOffset::$discriminant => (),)*
                    }
                }

                match value {
                    $(x if x == Self::$discriminant as u64 => Ok(Self::$discriminant),)*
                     _ => Err(value),
                }
            }
        }
        case! { DR, RSR, FR, FBRD, ILPR, IBRD, LCR_H, CR, FLS, IMSC, RIS, MIS, ICR, DMACR }
    }
}

pub mod registers {
    //! Device registers exposed as typed structs which are backed by arbitrary
    //! integer bitmaps. [`Data`], [`Control`], [`LineControl`], etc.
    //!
    //! All PL011 registers are essentially 32-bit wide, but are typed here as
    //! bitmaps with only the necessary width. That is, if a struct bitmap
    //! in this module is for example 16 bits long, it should be conceived
    //! as a 32-bit register where the unmentioned higher bits are always
    //! unused thus treated as zero when read or written.
    use bilge::prelude::*;

    // TODO: FIFO Mode has different semantics
    /// Data Register, `UARTDR`
    ///
    /// The `UARTDR` register is the data register.
    ///
    /// For words to be transmitted:
    ///
    /// - if the FIFOs are enabled, data written to this location is pushed onto
    ///   the transmit
    /// FIFO
    /// - if the FIFOs are not enabled, data is stored in the transmitter
    ///   holding register (the
    /// bottom word of the transmit FIFO).
    ///
    /// The write operation initiates transmission from the UART. The data is
    /// prefixed with a start bit, appended with the appropriate parity bit
    /// (if parity is enabled), and a stop bit. The resultant word is then
    /// transmitted.
    ///
    /// For received words:
    ///
    /// - if the FIFOs are enabled, the data byte and the 4-bit status (break,
    ///   frame, parity,
    /// and overrun) is pushed onto the 12-bit wide receive FIFO
    /// - if the FIFOs are not enabled, the data byte and status are stored in
    ///   the receiving
    /// holding register (the bottom word of the receive FIFO).
    ///
    /// The received data byte is read by performing reads from the `UARTDR`
    /// register along with the corresponding status information. The status
    /// information can also be read by a read of the `UARTRSR/UARTECR`
    /// register.
    ///
    /// # Note
    ///
    /// You must disable the UART before any of the control registers are
    /// reprogrammed. When the UART is disabled in the middle of
    /// transmission or reception, it completes the current character before
    /// stopping.
    ///
    /// # Source
    /// ARM DDI 0183G 3.3.1 Data Register, UARTDR
    #[bitsize(16)]
    #[derive(Clone, Copy, DebugBits, FromBits)]
    #[doc(alias = "UARTDR")]
    pub struct Data {
        _reserved: u4,
        pub data: u8,
        pub framing_error: bool,
        pub parity_error: bool,
        pub break_error: bool,
        pub overrun_error: bool,
    }

    // TODO: FIFO Mode has different semantics
    /// Receive Status Register / Error Clear Register, `UARTRSR/UARTECR`
    ///
    /// The UARTRSR/UARTECR register is the receive status register/error clear
    /// register. Receive status can also be read from the `UARTRSR`
    /// register. If the status is read from this register, then the status
    /// information for break, framing and parity corresponds to the
    /// data character read from the [Data register](Data), `UARTDR` prior to
    /// reading the UARTRSR register. The status information for overrun is
    /// set immediately when an overrun condition occurs.
    ///
    ///
    /// # Note
    /// The received data character must be read first from the [Data
    /// Register](Data), `UARTDR` before reading the error status associated
    /// with that data character from the `UARTRSR` register. This read
    /// sequence cannot be reversed, because the `UARTRSR` register is
    /// updated only when a read occurs from the `UARTDR` register. However,
    /// the status information can also be obtained by reading the `UARTDR`
    /// register
    ///
    /// # Source
    /// ARM DDI 0183G 3.3.2 Receive Status Register/Error Clear Register,
    /// UARTRSR/UARTECR
    #[bitsize(8)]
    #[derive(Clone, Copy, DebugBits, FromBits)]
    pub struct ReceiveStatusErrorClear {
        pub framing_error: bool,
        pub parity_error: bool,
        pub break_error: bool,
        pub overrun_error: bool,
        _reserved_unpredictable: u4,
    }

    impl ReceiveStatusErrorClear {
        pub fn reset(&mut self) {
            // All the bits are cleared to 0 on reset.
            *self = 0.into();
        }
    }

    impl Default for ReceiveStatusErrorClear {
        fn default() -> Self {
            0.into()
        }
    }

    #[bitsize(16)]
    #[derive(Clone, Copy, DebugBits, FromBits)]
    /// Flag Register, `UARTFR`
    #[doc(alias = "UARTFR")]
    pub struct Flags {
        /// CTS Clear to send. This bit is the complement of the UART clear to
        /// send, `nUARTCTS`, modem status input. That is, the bit is 1
        /// when `nUARTCTS` is LOW.
        pub clear_to_send: bool,
        /// DSR Data set ready. This bit is the complement of the UART data set
        /// ready, `nUARTDSR`, modem status input. That is, the bit is 1 when
        /// `nUARTDSR` is LOW.
        pub data_set_ready: bool,
        /// DCD Data carrier detect. This bit is the complement of the UART data
        /// carrier detect, `nUARTDCD`, modem status input. That is, the bit is
        /// 1 when `nUARTDCD` is LOW.
        pub data_carrier_detect: bool,
        /// BUSY UART busy. If this bit is set to 1, the UART is busy
        /// transmitting data. This bit remains set until the complete
        /// byte, including all the stop bits, has been sent from the
        /// shift register. This bit is set as soon as the transmit FIFO
        /// becomes non-empty, regardless of whether the UART is enabled
        /// or not.
        pub busy: bool,
        /// RXFE Receive FIFO empty. The meaning of this bit depends on the
        /// state of the FEN bit in the UARTLCR_H register. If the FIFO
        /// is disabled, this bit is set when the receive holding
        /// register is empty. If the FIFO is enabled, the RXFE bit is
        /// set when the receive FIFO is empty.
        pub receive_fifo_empty: bool,
        /// TXFF Transmit FIFO full. The meaning of this bit depends on the
        /// state of the FEN bit in the UARTLCR_H register. If the FIFO
        /// is disabled, this bit is set when the transmit holding
        /// register is full. If the FIFO is enabled, the TXFF bit is
        /// set when the transmit FIFO is full.
        pub transmit_fifo_full: bool,
        /// RXFF Receive FIFO full. The meaning of this bit depends on the state
        /// of the FEN bit in the UARTLCR_H register. If the FIFO is
        /// disabled, this bit is set when the receive holding register
        /// is full. If the FIFO is enabled, the RXFF bit is set when
        /// the receive FIFO is full.
        pub receive_fifo_full: bool,
        /// Transmit FIFO empty. The meaning of this bit depends on the state of
        /// the FEN bit in the [Line Control register](LineControl),
        /// `UARTLCR_H`. If the FIFO is disabled, this bit is set when the
        /// transmit holding register is empty. If the FIFO is enabled,
        /// the TXFE bit is set when the transmit FIFO is empty. This
        /// bit does not indicate if there is data in the transmit shift
        /// register.
        pub transmit_fifo_empty: bool,
        /// `RI`, is `true` when `nUARTRI` is `LOW`.
        pub ring_indicator: bool,
        _reserved_zero_no_modify: u7,
    }

    impl Flags {
        pub fn reset(&mut self) {
            // After reset TXFF, RXFF, and BUSY are 0, and TXFE and RXFE are 1
            self.set_receive_fifo_full(false);
            self.set_transmit_fifo_full(false);
            self.set_busy(false);
            self.set_receive_fifo_empty(true);
            self.set_transmit_fifo_empty(true);
        }
    }

    impl Default for Flags {
        fn default() -> Self {
            let mut ret: Self = 0.into();
            ret.reset();
            ret
        }
    }

    #[bitsize(16)]
    #[derive(Clone, Copy, DebugBits, FromBits)]
    /// Line Control Register, `UARTLCR_H`
    #[doc(alias = "UARTLCR_H")]
    pub struct LineControl {
        /// 15:8 - Reserved, do not modify, read as zero.
        _reserved_zero_no_modify: u8,
        /// 7 SPS Stick parity select.
        /// 0 = stick parity is disabled
        /// 1 = either:
        /// • if the EPS bit is 0 then the parity bit is transmitted and checked
        /// as a 1 • if the EPS bit is 1 then the parity bit is
        /// transmitted and checked as a 0. This bit has no effect when
        /// the PEN bit disables parity checking and generation. See Table 3-11
        /// on page 3-14 for the parity truth table.
        pub sticky_parity: bool,
        /// WLEN Word length. These bits indicate the number of data bits
        /// transmitted or received in a frame as follows: b11 = 8 bits
        /// b10 = 7 bits
        /// b01 = 6 bits
        /// b00 = 5 bits.
        pub word_length: WordLength,
        /// FEN Enable FIFOs:
        /// 0 = FIFOs are disabled (character mode) that is, the FIFOs become
        /// 1-byte-deep holding registers 1 = transmit and receive FIFO
        /// buffers are enabled (FIFO mode).
        pub fifos_enabled: Mode,
        /// 3 STP2 Two stop bits select. If this bit is set to 1, two stop bits
        /// are transmitted at the end of the frame. The receive
        /// logic does not check for two stop bits being received.
        pub two_stops_bits: bool,
        /// EPS Even parity select. Controls the type of parity the UART uses
        /// during transmission and reception:
        /// - 0 = odd parity. The UART generates or checks for an odd number of
        ///   1s in the data and parity bits.
        /// - 1 = even parity. The UART generates or checks for an even number
        ///   of 1s in the data and parity bits.
        /// This bit has no effect when the `PEN` bit disables parity checking
        /// and generation. See Table 3-11 on page 3-14 for the parity
        /// truth table.
        pub parity: Parity,
        /// 1 PEN Parity enable:
        ///
        /// - 0 = parity is disabled and no parity bit added to the data frame
        /// - 1 = parity checking and generation is enabled.
        ///
        /// See Table 3-11 on page 3-14 for the parity truth table.
        pub parity_enabled: bool,
        /// BRK Send break.
        ///
        /// If this bit is set to `1`, a low-level is continually output on the
        /// `UARTTXD` output, after completing transmission of the
        /// current character. For the proper execution of the break command,
        /// the software must set this bit for at least two complete
        /// frames. For normal use, this bit must be cleared to `0`.
        pub send_break: bool,
    }

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
        /// - 0 = odd parity. The UART generates or checks for an odd number of
        ///   1s in the data and parity bits.
        Odd = 0,
        /// - 1 = even parity. The UART generates or checks for an even number
        ///   of 1s in the data and parity bits.
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

    impl From<Mode> for bool {
        fn from(val: Mode) -> Self {
            matches!(val, Mode::FIFO)
        }
    }

    #[bitsize(2)]
    #[derive(Clone, Copy, Debug, Eq, FromBits, PartialEq)]
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
    /// The `UARTCR` register is the control register. All the bits are cleared
    /// to `0` on reset except for bits `9` and `8` that are set to `1`.
    ///
    /// # Source
    /// ARM DDI 0183G, 3.3.8 Control Register, `UARTCR`, Table 3-12
    #[bitsize(16)]
    #[doc(alias = "UARTCR")]
    #[derive(Clone, Copy, DebugBits, FromBits)]
    pub struct Control {
        /// `UARTEN` UART enable: 0 = UART is disabled. If the UART is disabled
        /// in the middle of transmission or reception, it completes the current
        /// character before stopping. 1 = the UART is enabled. Data
        /// transmission and reception occurs for either UART signals or SIR
        /// signals depending on the setting of the SIREN bit.
        pub enable_uart: bool,
        /// `SIREN` `SIR` enable: 0 = IrDA SIR ENDEC is disabled. `nSIROUT`
        /// remains LOW (no light pulse generated), and signal transitions on
        /// SIRIN have no effect. 1 = IrDA SIR ENDEC is enabled. Data is
        /// transmitted and received on nSIROUT and SIRIN. UARTTXD remains HIGH,
        /// in the marking state. Signal transitions on UARTRXD or modem status
        /// inputs have no effect. This bit has no effect if the UARTEN bit
        /// disables the UART.
        pub enable_sir: bool,
        /// `SIRLP` SIR low-power IrDA mode. This bit selects the IrDA encoding
        /// mode. If this bit is cleared to 0, low-level bits are transmitted as
        /// an active high pulse with a width of 3/ 16th of the bit period. If
        /// this bit is set to 1, low-level bits are transmitted with a pulse
        /// width that is 3 times the period of the IrLPBaud16 input signal,
        /// regardless of the selected bit rate. Setting this bit uses less
        /// power, but might reduce transmission distances.
        pub sir_lowpower_irda_mode: u1,
        /// Reserved, do not modify, read as zero.
        _reserved_zero_no_modify: u4,
        /// `LBE` Loopback enable. If this bit is set to 1 and the SIREN bit is
        /// set to 1 and the SIRTEST bit in the Test Control register, UARTTCR
        /// on page 4-5 is set to 1, then the nSIROUT path is inverted, and fed
        /// through to the SIRIN path. The SIRTEST bit in the test register must
        /// be set to 1 to override the normal half-duplex SIR operation. This
        /// must be the requirement for accessing the test registers during
        /// normal operation, and SIRTEST must be cleared to 0 when loopback
        /// testing is finished. This feature reduces the amount of external
        /// coupling required during system test. If this bit is set to 1, and
        /// the SIRTEST bit is set to 0, the UARTTXD path is fed through to the
        /// UARTRXD path. In either SIR mode or UART mode, when this bit is set,
        /// the modem outputs are also fed through to the modem inputs. This bit
        /// is cleared to 0 on reset, to disable loopback.
        pub enable_loopback: bool,
        /// `TXE` Transmit enable. If this bit is set to 1, the transmit section
        /// of the UART is enabled. Data transmission occurs for either UART
        /// signals, or SIR signals depending on the setting of the SIREN bit.
        /// When the UART is disabled in the middle of transmission, it
        /// completes the current character before stopping.
        pub enable_transmit: bool,
        /// `RXE` Receive enable. If this bit is set to 1, the receive section
        /// of the UART is enabled. Data reception occurs for either UART
        /// signals or SIR signals depending on the setting of the SIREN bit.
        /// When the UART is disabled in the middle of reception, it completes
        /// the current character before stopping.
        pub enable_receive: bool,
        /// `DTR` Data transmit ready. This bit is the complement of the UART
        /// data transmit ready, `nUARTDTR`, modem status output. That is, when
        /// the bit is programmed to a 1 then `nUARTDTR` is LOW.
        pub data_transmit_ready: bool,
        /// `RTS` Request to send. This bit is the complement of the UART
        /// request to send, `nUARTRTS`, modem status output. That is, when the
        /// bit is programmed to a 1 then `nUARTRTS` is LOW.
        pub request_to_send: bool,
        /// `Out1` This bit is the complement of the UART Out1 (`nUARTOut1`)
        /// modem status output. That is, when the bit is programmed to a 1 the
        /// output is 0. For DTE this can be used as Data Carrier Detect (DCD).
        pub out_1: bool,
        /// `Out2` This bit is the complement of the UART Out2 (`nUARTOut2`)
        /// modem status output. That is, when the bit is programmed to a 1, the
        /// output is 0. For DTE this can be used as Ring Indicator (RI).
        pub out_2: bool,
        /// `RTSEn` RTS hardware flow control enable. If this bit is set to 1,
        /// RTS hardware flow control is enabled. Data is only requested when
        /// there is space in the receive FIFO for it to be received.
        pub rts_hardware_flow_control_enable: bool,
        /// `CTSEn` CTS hardware flow control enable. If this bit is set to 1,
        /// CTS hardware flow control is enabled. Data is only transmitted when
        /// the `nUARTCTS` signal is asserted.
        pub cts_hardware_flow_control_enable: bool,
    }

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

    /// Interrupt status bits in UARTRIS, UARTMIS, UARTIMSC
    pub const INT_OE: u32 = 1 << 10;
    pub const INT_BE: u32 = 1 << 9;
    pub const INT_PE: u32 = 1 << 8;
    pub const INT_FE: u32 = 1 << 7;
    pub const INT_RT: u32 = 1 << 6;
    pub const INT_TX: u32 = 1 << 5;
    pub const INT_RX: u32 = 1 << 4;
    pub const INT_DSR: u32 = 1 << 3;
    pub const INT_DCD: u32 = 1 << 2;
    pub const INT_CTS: u32 = 1 << 1;
    pub const INT_RI: u32 = 1 << 0;
    pub const INT_E: u32 = INT_OE | INT_BE | INT_PE | INT_FE;
    pub const INT_MS: u32 = INT_RI | INT_DSR | INT_DCD | INT_CTS;

    #[repr(u32)]
    pub enum Interrupt {
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
    }

    impl Interrupt {
        pub const E: u32 = INT_OE | INT_BE | INT_PE | INT_FE;
        pub const MS: u32 = INT_RI | INT_DSR | INT_DCD | INT_CTS;
    }
}

// TODO: You must disable the UART before any of the control registers are
// reprogrammed. When the UART is disabled in the middle of transmission or
// reception, it completes the current character before stopping
