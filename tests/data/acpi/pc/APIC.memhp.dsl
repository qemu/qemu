/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/pc/APIC.memhp, Tue Aug  4 11:14:15 2020
 *
 * ACPI Data Table [APIC]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "APIC"    [Multiple APIC Description Table (MADT)]
[004h 0004   4]                 Table Length : 00000078
[008h 0008   1]                     Revision : 01
[009h 0009   1]                     Checksum : ED
[00Ah 0010   6]                       Oem ID : "BOCHS "
[010h 0016   8]                 Oem Table ID : "BXPCAPIC"
[018h 0024   4]                 Oem Revision : 00000001
[01Ch 0028   4]              Asl Compiler ID : "BXPC"
[020h 0032   4]        Asl Compiler Revision : 00000001

[024h 0036   4]           Local Apic Address : FEE00000
[028h 0040   4]        Flags (decoded below) : 00000001
                         PC-AT Compatibility : 1

[02Ch 0044   1]                Subtable Type : 00 [Processor Local APIC]
[02Dh 0045   1]                       Length : 08
[02Eh 0046   1]                 Processor ID : 00
[02Fh 0047   1]                Local Apic ID : 00
[030h 0048   4]        Flags (decoded below) : 00000001
                           Processor Enabled : 1
                      Runtime Online Capable : 0

[034h 0052   1]                Subtable Type : 01 [I/O APIC]
[035h 0053   1]                       Length : 0C
[036h 0054   1]                  I/O Apic ID : 00
[037h 0055   1]                     Reserved : 00
[038h 0056   4]                      Address : FEC00000
[03Ch 0060   4]                    Interrupt : 00000000

[040h 0064   1]                Subtable Type : 02 [Interrupt Source Override]
[041h 0065   1]                       Length : 0A
[042h 0066   1]                          Bus : 00
[043h 0067   1]                       Source : 00
[044h 0068   4]                    Interrupt : 00000002
[048h 0072   2]        Flags (decoded below) : 0000
                                    Polarity : 0
                                Trigger Mode : 0

[04Ah 0074   1]                Subtable Type : 02 [Interrupt Source Override]
[04Bh 0075   1]                       Length : 0A
[04Ch 0076   1]                          Bus : 00
[04Dh 0077   1]                       Source : 05
[04Eh 0078   4]                    Interrupt : 00000005
[052h 0082   2]        Flags (decoded below) : 000D
                                    Polarity : 1
                                Trigger Mode : 3

[054h 0084   1]                Subtable Type : 02 [Interrupt Source Override]
[055h 0085   1]                       Length : 0A
[056h 0086   1]                          Bus : 00
[057h 0087   1]                       Source : 09
[058h 0088   4]                    Interrupt : 00000009
[05Ch 0092   2]        Flags (decoded below) : 000D
                                    Polarity : 1
                                Trigger Mode : 3

[05Eh 0094   1]                Subtable Type : 02 [Interrupt Source Override]
[05Fh 0095   1]                       Length : 0A
[060h 0096   1]                          Bus : 00
[061h 0097   1]                       Source : 0A
[062h 0098   4]                    Interrupt : 0000000A
[066h 0102   2]        Flags (decoded below) : 000D
                                    Polarity : 1
                                Trigger Mode : 3

[068h 0104   1]                Subtable Type : 02 [Interrupt Source Override]
[069h 0105   1]                       Length : 0A
[06Ah 0106   1]                          Bus : 00
[06Bh 0107   1]                       Source : 0B
[06Ch 0108   4]                    Interrupt : 0000000B
[070h 0112   2]        Flags (decoded below) : 000D
                                    Polarity : 1
                                Trigger Mode : 3

[072h 0114   1]                Subtable Type : 04 [Local APIC NMI]
[073h 0115   1]                       Length : 06
[074h 0116   1]                 Processor ID : FF
[075h 0117   2]        Flags (decoded below) : 0000
                                    Polarity : 0
                                Trigger Mode : 0
[077h 0119   1]         Interrupt Input LINT : 01

Raw Table Data: Length 120 (0x78)

    0000: 41 50 49 43 78 00 00 00 01 ED 42 4F 43 48 53 20  // APICx.....BOCHS 
    0010: 42 58 50 43 41 50 49 43 01 00 00 00 42 58 50 43  // BXPCAPIC....BXPC
    0020: 01 00 00 00 00 00 E0 FE 01 00 00 00 00 08 00 00  // ................
    0030: 01 00 00 00 01 0C 00 00 00 00 C0 FE 00 00 00 00  // ................
    0040: 02 0A 00 00 02 00 00 00 00 00 02 0A 00 05 05 00  // ................
    0050: 00 00 0D 00 02 0A 00 09 09 00 00 00 0D 00 02 0A  // ................
    0060: 00 0A 0A 00 00 00 0D 00 02 0A 00 0B 0B 00 00 00  // ................
    0070: 0D 00 04 06 FF 00 00 01                          // ........
