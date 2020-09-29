/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/virt/SRAT.memhp, Tue Aug  4 11:14:15 2020
 *
 * ACPI Data Table [SRAT]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "SRAT"    [System Resource Affinity Table]
[004h 0004   4]                 Table Length : 000000E2
[008h 0008   1]                     Revision : 03
[009h 0009   1]                     Checksum : 5C
[00Ah 0010   6]                       Oem ID : "BOCHS "
[010h 0016   8]                 Oem Table ID : "BXPCSRAT"
[018h 0024   4]                 Oem Revision : 00000001
[01Ch 0028   4]              Asl Compiler ID : "BXPC"
[020h 0032   4]        Asl Compiler Revision : 00000001

[024h 0036   4]               Table Revision : 00000001
[028h 0040   8]                     Reserved : 0000000000000000

[030h 0048   1]                Subtable Type : 03 [GICC Affinity]
[031h 0049   1]                       Length : 12

[032h 0050   4]             Proximity Domain : 00000000
[036h 0054   4]           Acpi Processor UID : 00000000
[03Ah 0058   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
[03Eh 0062   4]                 Clock Domain : 00000000

[042h 0066   1]                Subtable Type : 01 [Memory Affinity]
[043h 0067   1]                       Length : 28

[044h 0068   4]             Proximity Domain : 00000000
[048h 0072   2]                    Reserved1 : 0000
[04Ah 0074   8]                 Base Address : 0000000040000000
[052h 0082   8]               Address Length : 0000000008000000
[05Ah 0090   4]                    Reserved2 : 00000000
[05Eh 0094   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[062h 0098   8]                    Reserved3 : 0000000000000000

[06Ah 0106   1]                Subtable Type : 01 [Memory Affinity]
[06Bh 0107   1]                       Length : 28

[06Ch 0108   4]             Proximity Domain : 00000001
[070h 0112   2]                    Reserved1 : 0000
[072h 0114   8]                 Base Address : 0000000048000000
[07Ah 0122   8]               Address Length : 0000000008000000
[082h 0130   4]                    Reserved2 : 00000000
[086h 0134   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[08Ah 0138   8]                    Reserved3 : 0000000000000000

[092h 0146   1]                Subtable Type : 01 [Memory Affinity]
[093h 0147   1]                       Length : 28

[094h 0148   4]             Proximity Domain : 00000001
[098h 0152   2]                    Reserved1 : 0000
[09Ah 0154   8]                 Base Address : 0000000088000000
[0A2h 0162   8]               Address Length : 0000000008000000
[0AAh 0170   4]                    Reserved2 : 00000000
[0AEh 0174   4]        Flags (decoded below) : 00000005
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 1
[0B2h 0178   8]                    Reserved3 : 0000000000000000

[0BAh 0186   1]                Subtable Type : 01 [Memory Affinity]
[0BBh 0187   1]                       Length : 28

[0BCh 0188   4]             Proximity Domain : 00000001
[0C0h 0192   2]                    Reserved1 : 0000
[0C2h 0194   8]                 Base Address : 0000000080000000
[0CAh 0202   8]               Address Length : 00000000F0000000
[0D2h 0210   4]                    Reserved2 : 00000000
[0D6h 0214   4]        Flags (decoded below) : 00000003
                                     Enabled : 1
                               Hot Pluggable : 1
                                Non-Volatile : 0
[0DAh 0218   8]                    Reserved3 : 0000000000000000

Raw Table Data: Length 226 (0xE2)

    0000: 53 52 41 54 E2 00 00 00 03 5C 42 4F 43 48 53 20  // SRAT.....\BOCHS 
    0010: 42 58 50 43 53 52 41 54 01 00 00 00 42 58 50 43  // BXPCSRAT....BXPC
    0020: 01 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0030: 03 12 00 00 00 00 00 00 00 00 01 00 00 00 00 00  // ................
    0040: 00 00 01 28 00 00 00 00 00 00 00 00 00 40 00 00  // ...(.........@..
    0050: 00 00 00 00 00 08 00 00 00 00 00 00 00 00 01 00  // ................
    0060: 00 00 00 00 00 00 00 00 00 00 01 28 01 00 00 00  // ...........(....
    0070: 00 00 00 00 00 48 00 00 00 00 00 00 00 08 00 00  // .....H..........
    0080: 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00  // ................
    0090: 00 00 01 28 01 00 00 00 00 00 00 00 00 88 00 00  // ...(............
    00A0: 00 00 00 00 00 08 00 00 00 00 00 00 00 00 05 00  // ................
    00B0: 00 00 00 00 00 00 00 00 00 00 01 28 01 00 00 00  // ...........(....
    00C0: 00 00 00 00 00 80 00 00 00 00 00 00 00 F0 00 00  // ................
    00D0: 00 00 00 00 00 00 03 00 00 00 00 00 00 00 00 00  // ................
    00E0: 00 00                                            // ..
