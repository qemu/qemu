/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/q35/TPM2.tis, Mon Sep 28 17:24:38 2020
 *
 * ACPI Data Table [TPM2]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "TPM2"    [Trusted Platform Module hardware interface table]
[004h 0004   4]                 Table Length : 0000004C
[008h 0008   1]                     Revision : 04
[009h 0009   1]                     Checksum : 72
[00Ah 0010   6]                       Oem ID : "BOCHS "
[010h 0016   8]                 Oem Table ID : "BXPCTPM2"
[018h 0024   4]                 Oem Revision : 00000001
[01Ch 0028   4]              Asl Compiler ID : "BXPC"
[020h 0032   4]        Asl Compiler Revision : 00000001

[024h 0036   2]               Platform Class : 0000
[026h 0038   2]                     Reserved : 0000
[028h 0040   8]              Control Address : 0000000000000000
[030h 0048   4]                 Start Method : 06 [Memory Mapped I/O]

[034h 0052  12]            Method Parameters : 00 00 00 00 00 00 00 00 00 00 00 00
[040h 0064   4]           Minimum Log Length : 00010000
[044h 0068   8]                  Log Address : 0000000007FF0000

Raw Table Data: Length 76 (0x4C)

    0000: 54 50 4D 32 4C 00 00 00 04 72 42 4F 43 48 53 20  // TPM2L....rBOCHS 
    0010: 42 58 50 43 54 50 4D 32 01 00 00 00 42 58 50 43  // BXPCTPM2....BXPC
    0020: 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
    0030: 06 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
    0040: 00 00 01 00 00 00 FF 07 00 00 00 00              // ............
