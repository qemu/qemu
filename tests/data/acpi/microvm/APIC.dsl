/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/microvm/APIC, Mon Sep 28 17:24:38 2020
 *
 * ACPI Data Table [APIC]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "APIC"    [Multiple APIC Description Table (MADT)]
[004h 0004   4]                 Table Length : 00000046
[008h 0008   1]                     Revision : 01
[009h 0009   1]                     Checksum : D7
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

[040h 0064   1]                Subtable Type : 04 [Local APIC NMI]
[041h 0065   1]                       Length : 06
[042h 0066   1]                 Processor ID : FF
[043h 0067   2]        Flags (decoded below) : 0000
                                    Polarity : 0
                                Trigger Mode : 0
[045h 0069   1]         Interrupt Input LINT : 01

Raw Table Data: Length 70 (0x46)

    0000: 41 50 49 43 46 00 00 00 01 D7 42 4F 43 48 53 20  // APICF.....BOCHS 
    0010: 42 58 50 43 41 50 49 43 01 00 00 00 42 58 50 43  // BXPCAPIC....BXPC
    0020: 01 00 00 00 00 00 E0 FE 01 00 00 00 00 08 00 00  // ................
    0030: 01 00 00 00 01 0C 00 00 00 00 C0 FE 00 00 00 00  // ................
    0040: 04 06 FF 00 00 01                                // ......
