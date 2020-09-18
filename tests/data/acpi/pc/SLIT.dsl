/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/pc/SLIT.memhp, Mon Sep 28 17:24:38 2020
 *
 * ACPI Data Table [SLIT]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "SLIT"    [System Locality Information Table]
[004h 0004   4]                 Table Length : 00000030
[008h 0008   1]                     Revision : 01
[009h 0009   1]                     Checksum : 2C
[00Ah 0010   6]                       Oem ID : "BOCHS "
[010h 0016   8]                 Oem Table ID : "BXPCSLIT"
[018h 0024   4]                 Oem Revision : 00000001
[01Ch 0028   4]              Asl Compiler ID : "BXPC"
[020h 0032   4]        Asl Compiler Revision : 00000001

[024h 0036   8]                   Localities : 0000000000000002
[02Ch 0044   2]                 Locality   0 : 0A 15
[02Eh 0046   2]                 Locality   1 : 15 0A

Raw Table Data: Length 48 (0x30)

    0000: 53 4C 49 54 30 00 00 00 01 2C 42 4F 43 48 53 20  // SLIT0....,BOCHS 
    0010: 42 58 50 43 53 4C 49 54 01 00 00 00 42 58 50 43  // BXPCSLIT....BXPC
    0020: 01 00 00 00 02 00 00 00 00 00 00 00 0A 15 15 0A  // ................
