/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/pc/SRAT.acpihmat, Tue Aug  4 11:14:15 2020
 *
 * ACPI Data Table [SRAT]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "SRAT"    [System Resource Affinity Table]
[004h 0004   4]                 Table Length : 00000118
[008h 0008   1]                     Revision : 01
[009h 0009   1]                     Checksum : C0
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

[040h 0064   1]                Subtable Type : 00 [Processor Local APIC/SAPIC Affinity]
[041h 0065   1]                       Length : 10

[042h 0066   1]      Proximity Domain Low(8) : 00
[043h 0067   1]                      Apic ID : 01
[044h 0068   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
[048h 0072   1]              Local Sapic EID : 00
[049h 0073   3]    Proximity Domain High(24) : 000000
[04Ch 0076   4]                 Clock Domain : 00000000

[050h 0080   1]                Subtable Type : 01 [Memory Affinity]
[051h 0081   1]                       Length : 28

[052h 0082   4]             Proximity Domain : 00000000
[056h 0086   2]                    Reserved1 : 0000
[058h 0088   8]                 Base Address : 0000000000000000
[060h 0096   8]               Address Length : 00000000000A0000
[068h 0104   4]                    Reserved2 : 00000000
[06Ch 0108   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[070h 0112   8]                    Reserved3 : 0000000000000000

[078h 0120   1]                Subtable Type : 01 [Memory Affinity]
[079h 0121   1]                       Length : 28

[07Ah 0122   4]             Proximity Domain : 00000000
[07Eh 0126   2]                    Reserved1 : 0000
[080h 0128   8]                 Base Address : 0000000000100000
[088h 0136   8]               Address Length : 0000000003F00000
[090h 0144   4]                    Reserved2 : 00000000
[094h 0148   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[098h 0152   8]                    Reserved3 : 0000000000000000

[0A0h 0160   1]                Subtable Type : 01 [Memory Affinity]
[0A1h 0161   1]                       Length : 28

[0A2h 0162   4]             Proximity Domain : 00000001
[0A6h 0166   2]                    Reserved1 : 0000
[0A8h 0168   8]                 Base Address : 0000000004000000
[0B0h 0176   8]               Address Length : 0000000004000000
[0B8h 0184   4]                    Reserved2 : 00000000
[0BCh 0188   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[0C0h 0192   8]                    Reserved3 : 0000000000000000

[0C8h 0200   1]                Subtable Type : 01 [Memory Affinity]
[0C9h 0201   1]                       Length : 28

[0CAh 0202   4]             Proximity Domain : 00000000
[0CEh 0206   2]                    Reserved1 : 0000
[0D0h 0208   8]                 Base Address : 0000000000000000
[0D8h 0216   8]               Address Length : 0000000000000000
[0E0h 0224   4]                    Reserved2 : 00000000
[0E4h 0228   4]        Flags (decoded below) : 00000000
                                     Enabled : 0
                               Hot Pluggable : 0
                                Non-Volatile : 0
[0E8h 0232   8]                    Reserved3 : 0000000000000000

[0F0h 0240   1]                Subtable Type : 01 [Memory Affinity]
[0F1h 0241   1]                       Length : 28

[0F2h 0242   4]             Proximity Domain : 00000001
[0F6h 0246   2]                    Reserved1 : 0000
[0F8h 0248   8]                 Base Address : 0000000100000000
[100h 0256   8]               Address Length : 00000000B8000000
[108h 0264   4]                    Reserved2 : 00000000
[10Ch 0268   4]        Flags (decoded below) : 00000003
                                     Enabled : 1
                               Hot Pluggable : 1
                                Non-Volatile : 0
[110h 0272   8]                    Reserved3 : 0000000000000000

Raw Table Data: Length 280 (0x118)

    0000: 53 52 41 54 18 01 00 00 01 C0 42 4F 43 48 53 20  // SRAT......BOCHS 
    0010: 42 58 50 43 53 52 41 54 01 00 00 00 42 58 50 43  // BXPCSRAT....BXPC
    0020: 01 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0030: 00 10 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0040: 00 10 00 01 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0050: 01 28 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // .(..............
    0060: 00 00 0A 00 00 00 00 00 00 00 00 00 01 00 00 00  // ................
    0070: 00 00 00 00 00 00 00 00 01 28 00 00 00 00 00 00  // .........(......
    0080: 00 00 10 00 00 00 00 00 00 00 F0 03 00 00 00 00  // ................
    0090: 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    00A0: 01 28 01 00 00 00 00 00 00 00 00 04 00 00 00 00  // .(..............
    00B0: 00 00 00 04 00 00 00 00 00 00 00 00 01 00 00 00  // ................
    00C0: 00 00 00 00 00 00 00 00 01 28 00 00 00 00 00 00  // .........(......
    00D0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
    00E0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
    00F0: 01 28 01 00 00 00 00 00 00 00 00 00 01 00 00 00  // .(..............
    0100: 00 00 00 B8 00 00 00 00 00 00 00 00 03 00 00 00  // ................
    0110: 00 00 00 00 00 00 00 00                          // ........
