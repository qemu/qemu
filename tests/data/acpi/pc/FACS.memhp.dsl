/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/pc/FACS.memhp, Tue Aug  4 11:14:15 2020
 *
 * ACPI Data Table [FACS]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "FACS"
[004h 0004   4]                       Length : 00000040
[008h 0008   4]           Hardware Signature : 00000000
[00Ch 0012   4]    32 Firmware Waking Vector : 00000000
[010h 0016   4]                  Global Lock : 00000000
[014h 0020   4]        Flags (decoded below) : 00000000
                      S4BIOS Support Present : 0
                  64-bit Wake Supported (V2) : 0
[018h 0024   8]    64 Firmware Waking Vector : 0000000000000000
[020h 0032   1]                      Version : 00
[021h 0033   3]                     Reserved : 000000
[024h 0036   4]    OspmFlags (decoded below) : 00000000
               64-bit Wake Env Required (V2) : 0

Raw Table Data: Length 64 (0x40)

    0000: 46 41 43 53 40 00 00 00 00 00 00 00 00 00 00 00  // FACS@...........
    0010: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
    0020: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
    0030: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
