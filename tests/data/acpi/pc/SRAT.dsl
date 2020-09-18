/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/pc/SRAT.numamem, Mon Sep 28 17:24:38 2020
 *
 * ACPI Data Table [SRAT]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "SRAT"    [System Resource Affinity Table]
[004h 0004   4]                 Table Length : 000000E0
[008h 0008   1]                     Revision : 01
[009h 0009   1]                     Checksum : F5
[00Ah 0010   6]                       Oem ID : "BOCHS "
[010h 0016   8]                 Oem Table ID : "BXPCSRAT"
[018h 0024   4]                 Oem Revision : 00000001
[01Ch 0028   4]              Asl Compiler ID : "BXPC"
[020h 0032   4]        Asl Compiler Revision : 00000001

[024h 0036   4]               Table Revision : 00000001
[028h 0040   8]                     Reserved : 0000000000000000

[030h 0048   1]                Subtable Type : 00 [Processor Local APIC/SAPIC Affinity]
[031h 0049   1]                       Length : 10

[032h 0050   1]      Proximity Domain Low(8) : 00
[033h 0051   1]                      Apic ID : 00
[034h 0052   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
[038h 0056   1]              Local Sapic EID : 00
[039h 0057   3]    Proximity Domain High(24) : 000000
[03Ch 0060   4]                 Clock Domain : 00000000

[040h 0064   1]                Subtable Type : 01 [Memory Affinity]
[041h 0065   1]                       Length : 28

[042h 0066   4]             Proximity Domain : 00000001
[046h 0070   2]                    Reserved1 : 0000
[048h 0072   8]                 Base Address : 0000000000000000
[050h 0080   8]               Address Length : 00000000000A0000
[058h 0088   4]                    Reserved2 : 00000000
[05Ch 0092   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[060h 0096   8]                    Reserved3 : 0000000000000000

[068h 0104   1]                Subtable Type : 01 [Memory Affinity]
[069h 0105   1]                       Length : 28

[06Ah 0106   4]             Proximity Domain : 00000001
[06Eh 0110   2]                    Reserved1 : 0000
[070h 0112   8]                 Base Address : 0000000000100000
[078h 0120   8]               Address Length : 0000000007F00000
[080h 0128   4]                    Reserved2 : 00000000
[084h 0132   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[088h 0136   8]                    Reserved3 : 0000000000000000

[090h 0144   1]                Subtable Type : 01 [Memory Affinity]
[091h 0145   1]                       Length : 28

[092h 0146   4]             Proximity Domain : 00000000
[096h 0150   2]                    Reserved1 : 0000
[098h 0152   8]                 Base Address : 0000000000000000
[0A0h 0160   8]               Address Length : 0000000000000000
[0A8h 0168   4]                    Reserved2 : 00000000
[0ACh 0172   4]        Flags (decoded below) : 00000000
                                     Enabled : 0
                               Hot Pluggable : 0
                                Non-Volatile : 0
[0B0h 0176   8]                    Reserved3 : 0000000000000000

[0B8h 0184   1]                Subtable Type : 01 [Memory Affinity]
[0B9h 0185   1]                       Length : 28

[0BAh 0186   4]             Proximity Domain : 00000000
[0BEh 0190   2]                    Reserved1 : 0000
[0C0h 0192   8]                 Base Address : 0000000000000000
[0C8h 0200   8]               Address Length : 0000000000000000
[0D0h 0208   4]                    Reserved2 : 00000000
[0D4h 0212   4]        Flags (decoded below) : 00000000
                                     Enabled : 0
                               Hot Pluggable : 0
                                Non-Volatile : 0
[0D8h 0216   8]                    Reserved3 : 0000000000000000

Raw Table Data: Length 224 (0xE0)

    0000: 53 52 41 54 E0 00 00 00 01 F5 42 4F 43 48 53 20  // SRAT......BOCHS 
    0010: 42 58 50 43 53 52 41 54 01 00 00 00 42 58 50 43  // BXPCSRAT....BXPC
    0020: 01 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0030: 00 10 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0040: 01 28 01 00 00 00 00 00 00 00 00 00 00 00 00 00  // .(..............
    0050: 00 00 0A 00 00 00 00 00 00 00 00 00 01 00 00 00  // ................
    0060: 00 00 00 00 00 00 00 00 01 28 01 00 00 00 00 00  // .........(......
    0070: 00 00 10 00 00 00 00 00 00 00 F0 07 00 00 00 00  // ................
    0080: 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0090: 01 28 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // .(..............
    00A0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
    00B0: 00 00 00 00 00 00 00 00 01 28 00 00 00 00 00 00  // .........(......
    00C0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
    00D0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
