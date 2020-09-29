/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/q35/HMAT.acpihmat, Mon Sep 28 17:24:38 2020
 *
 * ACPI Data Table [HMAT]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "HMAT"    [Heterogeneous Memory Attributes Table]
[004h 0004   4]                 Table Length : 00000118
[008h 0008   1]                     Revision : 02
[009h 0009   1]                     Checksum : 98
[00Ah 0010   6]                       Oem ID : "BOCHS "
[010h 0016   8]                 Oem Table ID : "BXPCHMAT"
[018h 0024   4]                 Oem Revision : 00000001
[01Ch 0028   4]              Asl Compiler ID : "BXPC"
[020h 0032   4]        Asl Compiler Revision : 00000001

[024h 0036   4]                     Reserved : 00000000

[028h 0040   2]               Structure Type : 0000 [Memory Proximity Domain Attributes]
[02Ah 0042   2]                     Reserved : 0000
[02Ch 0044   4]                       Length : 00000028
[030h 0048   2]        Flags (decoded below) : 0001
            Processor Proximity Domain Valid : 1
[032h 0050   2]                    Reserved1 : 0000
[034h 0052   4]   Processor Proximity Domain : 00000000
[038h 0056   4]      Memory Proximity Domain : 00000000
[03Ch 0060   4]                    Reserved2 : 00000000
[040h 0064   8]                    Reserved3 : 0000000000000000
[048h 0072   8]                    Reserved4 : 0000000000000000

[050h 0080   2]               Structure Type : 0000 [Memory Proximity Domain Attributes]
[052h 0082   2]                     Reserved : 0000
[054h 0084   4]                       Length : 00000028
[058h 0088   2]        Flags (decoded below) : 0001
            Processor Proximity Domain Valid : 1
[05Ah 0090   2]                    Reserved1 : 0000
[05Ch 0092   4]   Processor Proximity Domain : 00000000
[060h 0096   4]      Memory Proximity Domain : 00000001
[064h 0100   4]                    Reserved2 : 00000000
[068h 0104   8]                    Reserved3 : 0000000000000000
[070h 0112   8]                    Reserved4 : 0000000000000000

[078h 0120   2]               Structure Type : 0001 [System Locality Latency and Bandwidth Information]
[07Ah 0122   2]                     Reserved : 0000
[07Ch 0124   4]                       Length : 00000030
[080h 0128   1]        Flags (decoded below) : 00
                            Memory Hierarchy : 0
[081h 0129   1]                    Data Type : 00
[082h 0130   2]                    Reserved1 : 0000
[084h 0132   4] Initiator Proximity Domains # : 00000001
[088h 0136   4]   Target Proximity Domains # : 00000002
[08Ch 0140   4]                    Reserved2 : 00000000
[090h 0144   8]              Entry Base Unit : 00000000000003E8
[098h 0152   4] Initiator Proximity Domain List : 00000000
[09Ch 0156   4] Target Proximity Domain List : 00000000
[0A0h 0160   4] Target Proximity Domain List : 00000001
[0A4h 0164   2]                        Entry : 0001
[0A6h 0166   2]                        Entry : FFFE

[0A8h 0168   2]               Structure Type : 0001 [System Locality Latency and Bandwidth Information]
[0AAh 0170   2]                     Reserved : 0000
[0ACh 0172   4]                       Length : 00000030
[0B0h 0176   1]        Flags (decoded below) : 00
                            Memory Hierarchy : 0
[0B1h 0177   1]                    Data Type : 03
[0B2h 0178   2]                    Reserved1 : 0000
[0B4h 0180   4] Initiator Proximity Domains # : 00000001
[0B8h 0184   4]   Target Proximity Domains # : 00000002
[0BCh 0188   4]                    Reserved2 : 00000000
[0C0h 0192   8]              Entry Base Unit : 0000000000000001
[0C8h 0200   4] Initiator Proximity Domain List : 00000000
[0CCh 0204   4] Target Proximity Domain List : 00000000
[0D0h 0208   4] Target Proximity Domain List : 00000001
[0D4h 0212   2]                        Entry : FFFE
[0D6h 0214   2]                        Entry : 7FFF

[0D8h 0216   2]               Structure Type : 0002 [Memory Side Cache Information]
[0DAh 0218   2]                     Reserved : 0000
[0DCh 0220   4]                       Length : 00000020
[0E0h 0224   4]      Memory Proximity Domain : 00000000
[0E4h 0228   4]                    Reserved1 : 00000000
[0E8h 0232   8]       Memory Side Cache Size : 0000000000002800
[0F0h 0240   4] Cache Attributes (decoded below) : 00081111
                          Total Cache Levels : 1
                                 Cache Level : 1
                         Cache Associativity : 1
                                Write Policy : 1
                             Cache Line Size : 0008
[0F4h 0244   2]                    Reserved2 : 0000
[0F6h 0246   2]              SMBIOS Handle # : 0000

[0F8h 0248   2]               Structure Type : 0002 [Memory Side Cache Information]
[0FAh 0250   2]                     Reserved : 0000
[0FCh 0252   4]                       Length : 00000020
[100h 0256   4]      Memory Proximity Domain : 00000001
[104h 0260   4]                    Reserved1 : 00000000
[108h 0264   8]       Memory Side Cache Size : 0000000000002800
[110h 0272   4] Cache Attributes (decoded below) : 00081111
                          Total Cache Levels : 1
                                 Cache Level : 1
                         Cache Associativity : 1
                                Write Policy : 1
                             Cache Line Size : 0008
[114h 0276   2]                    Reserved2 : 0000
[116h 0278   2]              SMBIOS Handle # : 0000

Raw Table Data: Length 280 (0x118)

    0000: 48 4D 41 54 18 01 00 00 02 98 42 4F 43 48 53 20  // HMAT......BOCHS 
    0010: 42 58 50 43 48 4D 41 54 01 00 00 00 42 58 50 43  // BXPCHMAT....BXPC
    0020: 01 00 00 00 00 00 00 00 00 00 00 00 28 00 00 00  // ............(...
    0030: 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
    0040: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
    0050: 00 00 00 00 28 00 00 00 01 00 00 00 00 00 00 00  // ....(...........
    0060: 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
    0070: 00 00 00 00 00 00 00 00 01 00 00 00 30 00 00 00  // ............0...
    0080: 00 00 00 00 01 00 00 00 02 00 00 00 00 00 00 00  // ................
    0090: E8 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
    00A0: 01 00 00 00 01 00 FE FF 01 00 00 00 30 00 00 00  // ............0...
    00B0: 00 03 00 00 01 00 00 00 02 00 00 00 00 00 00 00  // ................
    00C0: 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
    00D0: 01 00 00 00 FE FF FF 7F 02 00 00 00 20 00 00 00  // ............ ...
    00E0: 00 00 00 00 00 00 00 00 00 28 00 00 00 00 00 00  // .........(......
    00F0: 11 11 08 00 00 00 00 00 02 00 00 00 20 00 00 00  // ............ ...
    0100: 01 00 00 00 00 00 00 00 00 28 00 00 00 00 00 00  // .........(......
    0110: 11 11 08 00 00 00 00 00                          // ........
