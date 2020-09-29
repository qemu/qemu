/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/pc/SRAT.cphp, Tue Aug  4 11:14:15 2020
 *
 * ACPI Data Table [SRAT]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "SRAT"    [System Resource Affinity Table]
[004h 0004   4]                 Table Length : 00000130
[008h 0008   1]                     Revision : 01
[009h 0009   1]                     Checksum : 36
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

[050h 0080   1]                Subtable Type : 00 [Processor Local APIC/SAPIC Affinity]
[051h 0081   1]                       Length : 10

[052h 0082   1]      Proximity Domain Low(8) : 00
[053h 0083   1]                      Apic ID : 02
[054h 0084   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
[058h 0088   1]              Local Sapic EID : 00
[059h 0089   3]    Proximity Domain High(24) : 000000
[05Ch 0092   4]                 Clock Domain : 00000000

[060h 0096   1]                Subtable Type : 00 [Processor Local APIC/SAPIC Affinity]
[061h 0097   1]                       Length : 10

[062h 0098   1]      Proximity Domain Low(8) : 01
[063h 0099   1]                      Apic ID : 04
[064h 0100   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
[068h 0104   1]              Local Sapic EID : 00
[069h 0105   3]    Proximity Domain High(24) : 000000
[06Ch 0108   4]                 Clock Domain : 00000000

[070h 0112   1]                Subtable Type : 00 [Processor Local APIC/SAPIC Affinity]
[071h 0113   1]                       Length : 10

[072h 0114   1]      Proximity Domain Low(8) : 01
[073h 0115   1]                      Apic ID : 05
[074h 0116   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
[078h 0120   1]              Local Sapic EID : 00
[079h 0121   3]    Proximity Domain High(24) : 000000
[07Ch 0124   4]                 Clock Domain : 00000000

[080h 0128   1]                Subtable Type : 00 [Processor Local APIC/SAPIC Affinity]
[081h 0129   1]                       Length : 10

[082h 0130   1]      Proximity Domain Low(8) : 01
[083h 0131   1]                      Apic ID : 06
[084h 0132   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
[088h 0136   1]              Local Sapic EID : 00
[089h 0137   3]    Proximity Domain High(24) : 000000
[08Ch 0140   4]                 Clock Domain : 00000000

[090h 0144   1]                Subtable Type : 01 [Memory Affinity]
[091h 0145   1]                       Length : 28

[092h 0146   4]             Proximity Domain : 00000000
[096h 0150   2]                    Reserved1 : 0000
[098h 0152   8]                 Base Address : 0000000000000000
[0A0h 0160   8]               Address Length : 00000000000A0000
[0A8h 0168   4]                    Reserved2 : 00000000
[0ACh 0172   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[0B0h 0176   8]                    Reserved3 : 0000000000000000

[0B8h 0184   1]                Subtable Type : 01 [Memory Affinity]
[0B9h 0185   1]                       Length : 28

[0BAh 0186   4]             Proximity Domain : 00000000
[0BEh 0190   2]                    Reserved1 : 0000
[0C0h 0192   8]                 Base Address : 0000000000100000
[0C8h 0200   8]               Address Length : 0000000003F00000
[0D0h 0208   4]                    Reserved2 : 00000000
[0D4h 0212   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[0D8h 0216   8]                    Reserved3 : 0000000000000000

[0E0h 0224   1]                Subtable Type : 01 [Memory Affinity]
[0E1h 0225   1]                       Length : 28

[0E2h 0226   4]             Proximity Domain : 00000001
[0E6h 0230   2]                    Reserved1 : 0000
[0E8h 0232   8]                 Base Address : 0000000004000000
[0F0h 0240   8]               Address Length : 0000000004000000
[0F8h 0248   4]                    Reserved2 : 00000000
[0FCh 0252   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[100h 0256   8]                    Reserved3 : 0000000000000000

[108h 0264   1]                Subtable Type : 01 [Memory Affinity]
[109h 0265   1]                       Length : 28

[10Ah 0266   4]             Proximity Domain : 00000000
[10Eh 0270   2]                    Reserved1 : 0000
[110h 0272   8]                 Base Address : 0000000000000000
[118h 0280   8]               Address Length : 0000000000000000
[120h 0288   4]                    Reserved2 : 00000000
[124h 0292   4]        Flags (decoded below) : 00000000
                                     Enabled : 0
                               Hot Pluggable : 0
                                Non-Volatile : 0
[128h 0296   8]                    Reserved3 : 0000000000000000

Raw Table Data: Length 304 (0x130)

    0000: 53 52 41 54 30 01 00 00 01 36 42 4F 43 48 53 20  // SRAT0....6BOCHS 
    0010: 42 58 50 43 53 52 41 54 01 00 00 00 42 58 50 43  // BXPCSRAT....BXPC
    0020: 01 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0030: 00 10 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0040: 00 10 00 01 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0050: 00 10 00 02 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0060: 00 10 01 04 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0070: 00 10 01 05 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0080: 00 10 01 06 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0090: 01 28 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // .(..............
    00A0: 00 00 0A 00 00 00 00 00 00 00 00 00 01 00 00 00  // ................
    00B0: 00 00 00 00 00 00 00 00 01 28 00 00 00 00 00 00  // .........(......
    00C0: 00 00 10 00 00 00 00 00 00 00 F0 03 00 00 00 00  // ................
    00D0: 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    00E0: 01 28 01 00 00 00 00 00 00 00 00 04 00 00 00 00  // .(..............
    00F0: 00 00 00 04 00 00 00 00 00 00 00 00 01 00 00 00  // ................
    0100: 00 00 00 00 00 00 00 00 01 28 00 00 00 00 00 00  // .........(......
    0110: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
    0120: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
