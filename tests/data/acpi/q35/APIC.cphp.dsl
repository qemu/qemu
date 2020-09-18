/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/q35/APIC.cphp, Tue Aug  4 11:14:15 2020
 *
 * ACPI Data Table [APIC]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "APIC"    [Multiple APIC Description Table (MADT)]
[004h 0004   4]                 Table Length : 000000A0
[008h 0008   1]                     Revision : 01
[009h 0009   1]                     Checksum : 7B
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

[03Ch 0060   1]                Subtable Type : 00 [Processor Local APIC]
[03Dh 0061   1]                       Length : 08
[03Eh 0062   1]                 Processor ID : 02
[03Fh 0063   1]                Local Apic ID : 02
[040h 0064   4]        Flags (decoded below) : 00000000
                           Processor Enabled : 0
                      Runtime Online Capable : 0

[044h 0068   1]                Subtable Type : 00 [Processor Local APIC]
[045h 0069   1]                       Length : 08
[046h 0070   1]                 Processor ID : 03
[047h 0071   1]                Local Apic ID : 04
[048h 0072   4]        Flags (decoded below) : 00000000
                           Processor Enabled : 0
                      Runtime Online Capable : 0

[04Ch 0076   1]                Subtable Type : 00 [Processor Local APIC]
[04Dh 0077   1]                       Length : 08
[04Eh 0078   1]                 Processor ID : 04
[04Fh 0079   1]                Local Apic ID : 05
[050h 0080   4]        Flags (decoded below) : 00000000
                           Processor Enabled : 0
                      Runtime Online Capable : 0

[054h 0084   1]                Subtable Type : 00 [Processor Local APIC]
[055h 0085   1]                       Length : 08
[056h 0086   1]                 Processor ID : 05
[057h 0087   1]                Local Apic ID : 06
[058h 0088   4]        Flags (decoded below) : 00000000
                           Processor Enabled : 0
                      Runtime Online Capable : 0

[05Ch 0092   1]                Subtable Type : 01 [I/O APIC]
[05Dh 0093   1]                       Length : 0C
[05Eh 0094   1]                  I/O Apic ID : 00
[05Fh 0095   1]                     Reserved : 00
[060h 0096   4]                      Address : FEC00000
[064h 0100   4]                    Interrupt : 00000000

[068h 0104   1]                Subtable Type : 02 [Interrupt Source Override]
[069h 0105   1]                       Length : 0A
[06Ah 0106   1]                          Bus : 00
[06Bh 0107   1]                       Source : 00
[06Ch 0108   4]                    Interrupt : 00000002
[070h 0112   2]        Flags (decoded below) : 0000
                                    Polarity : 0
                                Trigger Mode : 0

[072h 0114   1]                Subtable Type : 02 [Interrupt Source Override]
[073h 0115   1]                       Length : 0A
[074h 0116   1]                          Bus : 00
[075h 0117   1]                       Source : 05
[076h 0118   4]                    Interrupt : 00000005
[07Ah 0122   2]        Flags (decoded below) : 000D
                                    Polarity : 1
                                Trigger Mode : 3

[07Ch 0124   1]                Subtable Type : 02 [Interrupt Source Override]
[07Dh 0125   1]                       Length : 0A
[07Eh 0126   1]                          Bus : 00
[07Fh 0127   1]                       Source : 09
[080h 0128   4]                    Interrupt : 00000009
[084h 0132   2]        Flags (decoded below) : 000D
                                    Polarity : 1
                                Trigger Mode : 3

[086h 0134   1]                Subtable Type : 02 [Interrupt Source Override]
[087h 0135   1]                       Length : 0A
[088h 0136   1]                          Bus : 00
[089h 0137   1]                       Source : 0A
[08Ah 0138   4]                    Interrupt : 0000000A
[08Eh 0142   2]        Flags (decoded below) : 000D
                                    Polarity : 1
                                Trigger Mode : 3

[090h 0144   1]                Subtable Type : 02 [Interrupt Source Override]
[091h 0145   1]                       Length : 0A
[092h 0146   1]                          Bus : 00
[093h 0147   1]                       Source : 0B
[094h 0148   4]                    Interrupt : 0000000B
[098h 0152   2]        Flags (decoded below) : 000D
                                    Polarity : 1
                                Trigger Mode : 3

[09Ah 0154   1]                Subtable Type : 04 [Local APIC NMI]
[09Bh 0155   1]                       Length : 06
[09Ch 0156   1]                 Processor ID : FF
[09Dh 0157   2]        Flags (decoded below) : 0000
                                    Polarity : 0
                                Trigger Mode : 0
[09Fh 0159   1]         Interrupt Input LINT : 01

Raw Table Data: Length 160 (0xA0)

    0000: 41 50 49 43 A0 00 00 00 01 7B 42 4F 43 48 53 20  // APIC.....{BOCHS 
    0010: 42 58 50 43 41 50 49 43 01 00 00 00 42 58 50 43  // BXPCAPIC....BXPC
    0020: 01 00 00 00 00 00 E0 FE 01 00 00 00 00 08 00 00  // ................
    0030: 01 00 00 00 00 08 01 01 01 00 00 00 00 08 02 02  // ................
    0040: 00 00 00 00 00 08 03 04 00 00 00 00 00 08 04 05  // ................
    0050: 00 00 00 00 00 08 05 06 00 00 00 00 01 0C 00 00  // ................
    0060: 00 00 C0 FE 00 00 00 00 02 0A 00 00 02 00 00 00  // ................
    0070: 00 00 02 0A 00 05 05 00 00 00 0D 00 02 0A 00 09  // ................
    0080: 09 00 00 00 0D 00 02 0A 00 0A 0A 00 00 00 0D 00  // ................
    0090: 02 0A 00 0B 0B 00 00 00 0D 00 04 06 FF 00 00 01  // ................
