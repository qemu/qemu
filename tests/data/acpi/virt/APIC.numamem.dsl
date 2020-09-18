/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembly of tests/data/acpi/virt/APIC.numamem, Tue Aug  4 11:14:15 2020
 *
 * ACPI Data Table [APIC]
 *
 * Format: [HexOffset DecimalOffset ByteLength]  FieldName : FieldValue
 */

[000h 0000   4]                    Signature : "APIC"    [Multiple APIC Description Table (MADT)]
[004h 0004   4]                 Table Length : 000000A8
[008h 0008   1]                     Revision : 03
[009h 0009   1]                     Checksum : B3
[00Ah 0010   6]                       Oem ID : "BOCHS "
[010h 0016   8]                 Oem Table ID : "BXPCAPIC"
[018h 0024   4]                 Oem Revision : 00000001
[01Ch 0028   4]              Asl Compiler ID : "BXPC"
[020h 0032   4]        Asl Compiler Revision : 00000001

[024h 0036   4]           Local Apic Address : 00000000
[028h 0040   4]        Flags (decoded below) : 00000000
                         PC-AT Compatibility : 0

[02Ch 0044   1]                Subtable Type : 0C [Generic Interrupt Distributor]
[02Dh 0045   1]                       Length : 18
[02Eh 0046   2]                     Reserved : 0000
[030h 0048   4]        Local GIC Hardware ID : 00000000
[034h 0052   8]                 Base Address : 0000000008000000
[03Ch 0060   4]               Interrupt Base : 00000000
[040h 0064   1]                      Version : 02
[041h 0065   3]                     Reserved : 000000

[044h 0068   1]                Subtable Type : 0B [Generic Interrupt Controller]
[045h 0069   1]                       Length : 4C
[046h 0070   2]                     Reserved : 0000
[048h 0072   4]         CPU Interface Number : 00000000
[04Ch 0076   4]                Processor UID : 00000000
[050h 0080   4]        Flags (decoded below) : 00000001
                           Processor Enabled : 1
          Performance Interrupt Trigger Mode : 0
          Virtual GIC Interrupt Trigger Mode : 0
[054h 0084   4]     Parking Protocol Version : 00000000
[058h 0088   4]        Performance Interrupt : 00000017
[05Ch 0092   8]               Parked Address : 0000000000000000
[064h 0100   8]                 Base Address : 0000000008010000
[06Ch 0108   8]     Virtual GIC Base Address : 0000000008040000
[074h 0116   8]  Hypervisor GIC Base Address : 0000000008030000
[07Ch 0124   4]        Virtual GIC Interrupt : 00000000
[080h 0128   8]   Redistributor Base Address : 0000000000000000
[088h 0136   8]                    ARM MPIDR : 0000000000000000
/**** ACPI subtable terminates early - may be older version (dump table) */

[090h 0144   1]                Subtable Type : 0D [Generic MSI Frame]
[091h 0145   1]                       Length : 18
[092h 0146   2]                     Reserved : 0000
[094h 0148   4]                 MSI Frame ID : 00000000
[098h 0152   8]                 Base Address : 0000000008020000
[0A0h 0160   4]        Flags (decoded below) : 00000001
                                  Select SPI : 1
[0A4h 0164   2]                    SPI Count : 0040
[0A6h 0166   2]                     SPI Base : 0050

Raw Table Data: Length 168 (0xA8)

    0000: 41 50 49 43 A8 00 00 00 03 B3 42 4F 43 48 53 20  // APIC......BOCHS 
    0010: 42 58 50 43 41 50 49 43 01 00 00 00 42 58 50 43  // BXPCAPIC....BXPC
    0020: 01 00 00 00 00 00 00 00 00 00 00 00 0C 18 00 00  // ................
    0030: 00 00 00 00 00 00 00 08 00 00 00 00 00 00 00 00  // ................
    0040: 02 00 00 00 0B 4C 00 00 00 00 00 00 00 00 00 00  // .....L..........
    0050: 01 00 00 00 00 00 00 00 17 00 00 00 00 00 00 00  // ................
    0060: 00 00 00 00 00 00 01 08 00 00 00 00 00 00 04 08  // ................
    0070: 00 00 00 00 00 00 03 08 00 00 00 00 00 00 00 00  // ................
    0080: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  // ................
    0090: 0D 18 00 00 00 00 00 00 00 00 02 08 00 00 00 00  // ................
    00A0: 01 00 00 00 40 00 50 00                          // ....@.P.
