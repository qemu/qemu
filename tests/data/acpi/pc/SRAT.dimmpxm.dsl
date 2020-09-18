/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/pc/SRAT.dimmpxm, Tue Aug  4 11:14:15 2020
 *
 * ACPI Data Table [SRAT]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "SRAT"    [System Resource Affinity Table]
[004h 0004   4]                 Table Length : 00000188
[008h 0008   1]                     Revision : 01
[009h 0009   1]                     Checksum : 68
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

[042h 0066   1]      Proximity Domain Low(8) : 01
[043h 0067   1]                      Apic ID : 01
[044h 0068   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
[048h 0072   1]              Local Sapic EID : 00
[049h 0073   3]    Proximity Domain High(24) : 000000
[04Ch 0076   4]                 Clock Domain : 00000000

[050h 0080   1]                Subtable Type : 00 [Processor Local APIC/SAPIC Affinity]
[051h 0081   1]                       Length : 10

[052h 0082   1]      Proximity Domain Low(8) : 02
[053h 0083   1]                      Apic ID : 02
[054h 0084   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
[058h 0088   1]              Local Sapic EID : 00
[059h 0089   3]    Proximity Domain High(24) : 000000
[05Ch 0092   4]                 Clock Domain : 00000000

[060h 0096   1]                Subtable Type : 00 [Processor Local APIC/SAPIC Affinity]
[061h 0097   1]                       Length : 10

[062h 0098   1]      Proximity Domain Low(8) : 03
[063h 0099   1]                      Apic ID : 03
[064h 0100   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
[068h 0104   1]              Local Sapic EID : 00
[069h 0105   3]    Proximity Domain High(24) : 000000
[06Ch 0108   4]                 Clock Domain : 00000000

[070h 0112   1]                Subtable Type : 01 [Memory Affinity]
[071h 0113   1]                       Length : 28

[072h 0114   4]             Proximity Domain : 00000000
[076h 0118   2]                    Reserved1 : 0000
[078h 0120   8]                 Base Address : 0000000000000000
[080h 0128   8]               Address Length : 00000000000A0000
[088h 0136   4]                    Reserved2 : 00000000
[08Ch 0140   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[090h 0144   8]                    Reserved3 : 0000000000000000

[098h 0152   1]                Subtable Type : 01 [Memory Affinity]
[099h 0153   1]                       Length : 28

[09Ah 0154   4]             Proximity Domain : 00000000
[09Eh 0158   2]                    Reserved1 : 0000
[0A0h 0160   8]                 Base Address : 0000000000100000
[0A8h 0168   8]               Address Length : 0000000001F00000
[0B0h 0176   4]                    Reserved2 : 00000000
[0B4h 0180   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[0B8h 0184   8]                    Reserved3 : 0000000000000000

[0C0h 0192   1]                Subtable Type : 01 [Memory Affinity]
[0C1h 0193   1]                       Length : 28

[0C2h 0194   4]             Proximity Domain : 00000001
[0C6h 0198   2]                    Reserved1 : 0000
[0C8h 0200   8]                 Base Address : 0000000002000000
[0D0h 0208   8]               Address Length : 0000000002000000
[0D8h 0216   4]                    Reserved2 : 00000000
[0DCh 0220   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[0E0h 0224   8]                    Reserved3 : 0000000000000000

[0E8h 0232   1]                Subtable Type : 01 [Memory Affinity]
[0E9h 0233   1]                       Length : 28

[0EAh 0234   4]             Proximity Domain : 00000002
[0EEh 0238   2]                    Reserved1 : 0000
[0F0h 0240   8]                 Base Address : 0000000004000000
[0F8h 0248   8]               Address Length : 0000000002000000
[100h 0256   4]                    Reserved2 : 00000000
[104h 0260   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[108h 0264   8]                    Reserved3 : 0000000000000000

[110h 0272   1]                Subtable Type : 01 [Memory Affinity]
[111h 0273   1]                       Length : 28

[112h 0274   4]             Proximity Domain : 00000003
[116h 0278   2]                    Reserved1 : 0000
[118h 0280   8]                 Base Address : 0000000006000000
[120h 0288   8]               Address Length : 0000000002000000
[128h 0296   4]                    Reserved2 : 00000000
[12Ch 0300   4]        Flags (decoded below) : 00000001
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 0
[130h 0304   8]                    Reserved3 : 0000000000000000

[138h 0312   1]                Subtable Type : 01 [Memory Affinity]
[139h 0313   1]                       Length : 28

[13Ah 0314   4]             Proximity Domain : 00000002
[13Eh 0318   2]                    Reserved1 : 0000
[140h 0320   8]                 Base Address : 0000000108000000
[148h 0328   8]               Address Length : 0000000008000000
[150h 0336   4]                    Reserved2 : 00000000
[154h 0340   4]        Flags (decoded below) : 00000005
                                     Enabled : 1
                               Hot Pluggable : 0
                                Non-Volatile : 1
[158h 0344   8]                    Reserved3 : 0000000000000000

[160h 0352   1]                Subtable Type : 01 [Memory Affinity]
[161h 0353   1]                       Length : 28

[162h 0354   4]             Proximity Domain : 00000003
[166h 0358   2]                    Reserved1 : 0000
[168h 0360   8]                 Base Address : 0000000100000000
[170h 0368   8]               Address Length : 00000000F8000000
[178h 0376   4]                    Reserved2 : 00000000
[17Ch 0380   4]        Flags (decoded below) : 00000003
                                     Enabled : 1
                               Hot Pluggable : 1
                                Non-Volatile : 0
[180h 0384   8]                    Reserved3 : 0000000000000000

Raw Table Data: Length 392 (0x188)

    0000: 53 52 41 54 88 01 00 00 01 68 42 4F 43 48 53 20  // SRAT.....hBOCHS 
    0010: 42 58 50 43 53 52 41 54 01 00 00 00 42 58 50 43  // BXPCSRAT....BXPC
    0020: 01 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0030: 00 10 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0040: 00 10 01 01 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0050: 00 10 02 02 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0060: 00 10 03 03 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0070: 01 28 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // .(..............
    0080: 00 00 0A 00 00 00 00 00 00 00 00 00 01 00 00 00  // ................
    0090: 00 00 00 00 00 00 00 00 01 28 00 00 00 00 00 00  // .........(......
    00A0: 00 00 10 00 00 00 00 00 00 00 F0 01 00 00 00 00  // ................
    00B0: 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    00C0: 01 28 01 00 00 00 00 00 00 00 00 02 00 00 00 00  // .(..............
    00D0: 00 00 00 02 00 00 00 00 00 00 00 00 01 00 00 00  // ................
    00E0: 00 00 00 00 00 00 00 00 01 28 02 00 00 00 00 00  // .........(......
    00F0: 00 00 00 04 00 00 00 00 00 00 00 02 00 00 00 00  // ................
    0100: 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00  // ................
    0110: 01 28 03 00 00 00 00 00 00 00 00 06 00 00 00 00  // .(..............
    0120: 00 00 00 02 00 00 00 00 00 00 00 00 01 00 00 00  // ................
    0130: 00 00 00 00 00 00 00 00 01 28 02 00 00 00 00 00  // .........(......
    0140: 00 00 00 08 01 00 00 00 00 00 00 08 00 00 00 00  // ................
    0150: 00 00 00 00 05 00 00 00 00 00 00 00 00 00 00 00  // ................
    0160: 01 28 03 00 00 00 00 00 00 00 00 00 01 00 00 00  // .(..............
    0170: 00 00 00 F8 00 00 00 00 00 00 00 00 03 00 00 00  // ................
    0180: 00 00 00 00 00 00 00 00                          // ........
