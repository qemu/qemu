/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/pc/APIC.dimmpxm, Tue Aug  4 11:14:15 2020
 *
 * ACPI Data Table [APIC]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "APIC"    [Multiple APIC Description Table (MADT)]
[004h 0004   4]                 Table Length : 00000090
[008h 0008   1]                     Revision : 01
[009h 0009   1]                     Checksum : AE
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
[040h 0064   4]        Flags (decoded below) : 00000001
                           Processor Enabled : 1
                      Runtime Online Capable : 0

[044h 0068   1]                Subtable Type : 00 [Processor Local APIC]
[045h 0069   1]                       Length : 08
[046h 0070   1]                 Processor ID : 03
[047h 0071   1]                Local Apic ID : 03
[048h 0072   4]        Flags (decoded below) : 00000001
                           Processor Enabled : 1
                      Runtime Online Capable : 0

[04Ch 0076   1]                Subtable Type : 01 [I/O APIC]
[04Dh 0077   1]                       Length : 0C
[04Eh 0078   1]                  I/O Apic ID : 00
[04Fh 0079   1]                     Reserved : 00
[050h 0080   4]                      Address : FEC00000
[054h 0084   4]                    Interrupt : 00000000

[058h 0088   1]                Subtable Type : 02 [Interrupt Source Override]
[059h 0089   1]                       Length : 0A
[05Ah 0090   1]                          Bus : 00
[05Bh 0091   1]                       Source : 00
[05Ch 0092   4]                    Interrupt : 00000002
[060h 0096   2]        Flags (decoded below) : 0000
                                    Polarity : 0
                                Trigger Mode : 0

[062h 0098   1]                Subtable Type : 02 [Interrupt Source Override]
[063h 0099   1]                       Length : 0A
[064h 0100   1]                          Bus : 00
[065h 0101   1]                       Source : 05
[066h 0102   4]                    Interrupt : 00000005
[06Ah 0106   2]        Flags (decoded below) : 000D
                                    Polarity : 1
                                Trigger Mode : 3

[06Ch 0108   1]                Subtable Type : 02 [Interrupt Source Override]
[06Dh 0109   1]                       Length : 0A
[06Eh 0110   1]                          Bus : 00
[06Fh 0111   1]                       Source : 09
[070h 0112   4]                    Interrupt : 00000009
[074h 0116   2]        Flags (decoded below) : 000D
                                    Polarity : 1
                                Trigger Mode : 3

[076h 0118   1]                Subtable Type : 02 [Interrupt Source Override]
[077h 0119   1]                       Length : 0A
[078h 0120   1]                          Bus : 00
[079h 0121   1]                       Source : 0A
[07Ah 0122   4]                    Interrupt : 0000000A
[07Eh 0126   2]        Flags (decoded below) : 000D
                                    Polarity : 1
                                Trigger Mode : 3

[080h 0128   1]                Subtable Type : 02 [Interrupt Source Override]
[081h 0129   1]                       Length : 0A
[082h 0130   1]                          Bus : 00
[083h 0131   1]                       Source : 0B
[084h 0132   4]                    Interrupt : 0000000B
[088h 0136   2]        Flags (decoded below) : 000D
                                    Polarity : 1
                                Trigger Mode : 3

[08Ah 0138   1]                Subtable Type : 04 [Local APIC NMI]
[08Bh 0139   1]                       Length : 06
[08Ch 0140   1]                 Processor ID : FF
[08Dh 0141   2]        Flags (decoded below) : 0000
                                    Polarity : 0
                                Trigger Mode : 0
[08Fh 0143   1]         Interrupt Input LINT : 01

Raw Table Data: Length 144 (0x90)

    0000: 41 50 49 43 90 00 00 00 01 AE 42 4F 43 48 53 20  // APIC......BOCHS 
    0010: 42 58 50 43 41 50 49 43 01 00 00 00 42 58 50 43  // BXPCAPIC....BXPC
    0020: 01 00 00 00 00 00 E0 FE 01 00 00 00 00 08 00 00  // ................
    0030: 01 00 00 00 00 08 01 01 01 00 00 00 00 08 02 02  // ................
    0040: 01 00 00 00 00 08 03 03 01 00 00 00 01 0C 00 00  // ................
    0050: 00 00 C0 FE 00 00 00 00 02 0A 00 00 02 00 00 00  // ................
    0060: 00 00 02 0A 00 05 05 00 00 00 0D 00 02 0A 00 09  // ................
    0070: 09 00 00 00 0D 00 02 0A 00 0A 0A 00 00 00 0D 00  // ................
    0080: 02 0A 00 0B 0B 00 00 00 0D 00 04 06 FF 00 00 01  // ................
