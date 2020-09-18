/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/virt/SPCR.memhp, Tue Aug  4 11:14:15 2020
 *
 * ACPI Data Table [SPCR]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "SPCR"    [Serial Port Console Redirection table]
[004h 0004   4]                 Table Length : 00000050
[008h 0008   1]                     Revision : 02
[009h 0009   1]                     Checksum : 13
[00Ah 0010   6]                       Oem ID : "BOCHS "
[010h 0016   8]                 Oem Table ID : "BXPCSPCR"
[018h 0024   4]                 Oem Revision : 00000001
[01Ch 0028   4]              Asl Compiler ID : "BXPC"
[020h 0032   4]        Asl Compiler Revision : 00000001

[024h 0036   1]               Interface Type : 03
[025h 0037   3]                     Reserved : 000000

[028h 0040  12]         Serial Port Register : [Generic Address Structure]
[028h 0040   1]                     Space ID : 00 [SystemMemory]
[029h 0041   1]                    Bit Width : 08
[02Ah 0042   1]                   Bit Offset : 00
[02Bh 0043   1]         Encoded Access Width : 01 [Byte Access:8]
[02Ch 0044   8]                      Address : 0000000009000000

[034h 0052   1]               Interrupt Type : 08
[035h 0053   1]          PCAT-compatible IRQ : 00
[036h 0054   4]                    Interrupt : 00000021
[03Ah 0058   1]                    Baud Rate : 03
[03Bh 0059   1]                       Parity : 00
[03Ch 0060   1]                    Stop Bits : 01
[03Dh 0061   1]                 Flow Control : 02
[03Eh 0062   1]                Terminal Type : 00
[04Ch 0076   1]                     Reserved : 00
[040h 0064   2]                PCI Device ID : FFFF
[042h 0066   2]                PCI Vendor ID : FFFF
[044h 0068   1]                      PCI Bus : 00
[045h 0069   1]                   PCI Device : 00
[046h 0070   1]                 PCI Function : 00
[047h 0071   4]                    PCI Flags : 00000000
[04Bh 0075   1]                  PCI Segment : 00
[04Ch 0076   4]                     Reserved : 00000000

Raw Table Data: Length 80 (0x50)

    0000: 53 50 43 52 50 00 00 00 02 13 42 4F 43 48 53 20  // SPCRP.....BOCHS 
    0010: 42 58 50 43 53 50 43 52 01 00 00 00 42 58 50 43  // BXPCSPCR....BXPC
    0020: 01 00 00 00 03 00 00 00 00 08 00 01 00 00 00 09  // ................
    0030: 00 00 00 00 08 00 21 00 00 00 03 00 01 02 00 00  // ......!.........
    0040: FF FF FF FF 00 00 00 00 00 00 00 00 00 00 00 00  // ................
