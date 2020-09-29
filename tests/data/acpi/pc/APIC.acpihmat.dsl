/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/pc/APIC.acpihmat, Tue Aug  4 11:14:15 2020
 *
 * ACPI Data Table [APIC]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "APIC"    [Multiple APIC Description Table (MADT)]
[004h 0004   4]                 Table Length : 00000080
[008h 0008   1]                     Revision : 01
[009h 0009   1]                     Checksum : DA
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

[034h 0052   1]                Subtable Type : 00 [Processor Local APIC]
[035h 0053   1]                       Length : 08
[036h 0054   1]                 Processor ID : 01
[037h 0055   1]                Local Apic ID : 01
[038h 0056   4]        Flags (decoded below) : 00000001
                           Processor Enabled : 1
                      Runtime Online Capable : 0

[03Ch 0060   1]                Subtable Type : 01 [I/O APIC]
[03Dh 0061   1]                       Length : 0C
[03Eh 0062   1]                  I/O Apic ID : 00
[03Fh 0063   1]                     Reserved : 00
[040h 0064   4]                      Address : FEC00000
[044h 0068   4]                    Interrupt : 00000000

[048h 0072   1]                Subtable Type : 02 [Interrupt Source Override]
[049h 0073   1]                       Length : 0A
[04Ah 0074   1]                          Bus : 00
[04Bh 0075   1]                       Source : 00
[04Ch 0076   4]                    Interrupt : 00000002
[050h 0080   2]        Flags (decoded below) : 0000
                                    Polarity : 0
                                Trigger Mode : 0

[052h 0082   1]                Subtable Type : 02 [Interrupt Source Override]
[053h 0083   1]                       Length : 0A
[054h 0084   1]                          Bus : 00
[055h 0085   1]                       Source : 05
[056h 0086   4]                    Interrupt : 00000005
[05Ah 0090   2]        Flags (decoded below) : 000D
                                    Polarity : 1
                                Trigger Mode : 3

[05Ch 0092   1]                Subtable Type : 02 [Interrupt Source Override]
[05Dh 0093   1]                       Length : 0A
[05Eh 0094   1]                          Bus : 00
[05Fh 0095   1]                       Source : 09
[060h 0096   4]                    Interrupt : 00000009
[064h 0100   2]        Flags (decoded below) : 000D
                                    Polarity : 1
                                Trigger Mode : 3

[066h 0102   1]                Subtable Type : 02 [Interrupt Source Override]
[067h 0103   1]                       Length : 0A
[068h 0104   1]                          Bus : 00
[069h 0105   1]                       Source : 0A
[06Ah 0106   4]                    Interrupt : 0000000A
[06Eh 0110   2]        Flags (decoded below) : 000D
                                    Polarity : 1
                                Trigger Mode : 3

[070h 0112   1]                Subtable Type : 02 [Interrupt Source Override]
[071h 0113   1]                       Length : 0A
[072h 0114   1]                          Bus : 00
[073h 0115   1]                       Source : 0B
[074h 0116   4]                    Interrupt : 0000000B
[078h 0120   2]        Flags (decoded below) : 000D
                                    Polarity : 1
                                Trigger Mode : 3

[07Ah 0122   1]                Subtable Type : 04 [Local APIC NMI]
[07Bh 0123   1]                       Length : 06
[07Ch 0124   1]                 Processor ID : FF
[07Dh 0125   2]        Flags (decoded below) : 0000
                                    Polarity : 0
                                Trigger Mode : 0
[07Fh 0127   1]         Interrupt Input LINT : 01

Raw Table Data: Length 128 (0x80)

    0000: 41 50 49 43 80 00 00 00 01 DA 42 4F 43 48 53 20  // APIC......BOCHS 
    0010: 42 58 50 43 41 50 49 43 01 00 00 00 42 58 50 43  // BXPCAPIC....BXPC
    0020: 01 00 00 00 00 00 E0 FE 01 00 00 00 00 08 00 00  // ................
    0030: 01 00 00 00 00 08 01 01 01 00 00 00 01 0C 00 00  // ................
    0040: 00 00 C0 FE 00 00 00 00 02 0A 00 00 02 00 00 00  // ................
    0050: 00 00 02 0A 00 05 05 00 00 00 0D 00 02 0A 00 09  // ................
    0060: 09 00 00 00 0D 00 02 0A 00 0A 0A 00 00 00 0D 00  // ................
    0070: 02 0A 00 0B 0B 00 00 00 0D 00 04 06 FF 00 00 01  // ................
