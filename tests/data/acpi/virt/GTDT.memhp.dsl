/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/virt/GTDT.memhp, Tue Aug  4 11:14:15 2020
 *
 * ACPI Data Table [GTDT]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "GTDT"    [Generic Timer Description Table]
[004h 0004   4]                 Table Length : 00000060
[008h 0008   1]                     Revision : 02
[009h 0009   1]                     Checksum : D9
[00Ah 0010   6]                       Oem ID : "BOCHS "
[010h 0016   8]                 Oem Table ID : "BXPCGTDT"
[018h 0024   4]                 Oem Revision : 00000001
[01Ch 0028   4]              Asl Compiler ID : "BXPC"
[020h 0032   4]        Asl Compiler Revision : 00000001

[024h 0036   8]        Counter Block Address : 0000000000000000
[02Ch 0044   4]                     Reserved : 00000000

[030h 0048   4]         Secure EL1 Interrupt : 0000001D
[034h 0052   4]    EL1 Flags (decoded below) : 00000000
                                Trigger Mode : 0
                                    Polarity : 0
                                   Always On : 0

[038h 0056   4]     Non-Secure EL1 Interrupt : 0000001E
[03Ch 0060   4]   NEL1 Flags (decoded below) : 00000004
                                Trigger Mode : 0
                                    Polarity : 0
                                   Always On : 1

[040h 0064   4]      Virtual Timer Interrupt : 0000001B
[044h 0068   4]     VT Flags (decoded below) : 00000000
                                Trigger Mode : 0
                                    Polarity : 0
                                   Always On : 0

[048h 0072   4]     Non-Secure EL2 Interrupt : 0000001A
[04Ch 0076   4]   NEL2 Flags (decoded below) : 00000000
                                Trigger Mode : 0
                                    Polarity : 0
                                   Always On : 0
[050h 0080   8]   Counter Read Block Address : 0000000000000000

[058h 0088   4]         Platform Timer Count : 00000000
[05Ch 0092   4]        Platform Timer Offset : 00000000

Raw Table Data: Length 96 (0x60)

    0000: 47 54 44 54 60 00 00 00 02 D9 42 4F 43 48 53 20  // GTDT`.....BOCHS 
    0010: 42 58 50 43 47 54 44 54 01 00 00 00 42 58 50 43  // BXPCGTDT....BXPC
    0020: 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
    0030: 1D 00 00 00 00 00 00 00 1E 00 00 00 04 00 00 00  // ................
    0040: 1B 00 00 00 00 00 00 00 1A 00 00 00 00 00 00 00  // ................
    0050: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
