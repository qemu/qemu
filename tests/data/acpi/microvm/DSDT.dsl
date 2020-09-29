/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembling to symbolic ASL+ operators
 *
 * Disassembly of tests/data/acpi/microvm/DSDT, Mon Sep 28 17:24:38 2020
 *
 * Original Table Header:
 *     Signature        "DSDT"
 *     Length           0x0000016D (365)
 *     Revision         0x02
 *     Checksum         0x62
 *     OEM ID           "BOCHS "
 *     OEM Table ID     "BXPCDSDT"
 *     OEM Revision     0x00000001 (1)
 *     Compiler ID      "BXPC"
 *     Compiler Version 0x00000001 (1)
 */
DefinitionBlock ("", "DSDT", 2, "BOCHS ", "BXPCDSDT", 0x00000001)
{
    Scope (_SB)
    {
        Device (FWCF)
        {
            Name (_HID, "QEMU0002")  // _HID: Hardware ID
            Name (_STA, 0x0B)  // _STA: Status
            Name (_CRS, ResourceTemplate ()  // _CRS: Current Resource Settings
            {
                IO (Decode16,
                    0x0510,             // Range Minimum
                    0x0510,             // Range Maximum
                    0x01,               // Alignment
                    0x0C,               // Length
                    )
            })
        }

        Device (COM1)
        {
            Name (_HID, EisaId ("PNP0501") /* 16550A-compatible COM Serial Port */)  // _HID: Hardware ID
            Name (_UID, One)  // _UID: Unique ID
            Name (_STA, 0x0F)  // _STA: Status
            Name (_CRS, ResourceTemplate ()  // _CRS: Current Resource Settings
            {
                IO (Decode16,
                    0x03F8,             // Range Minimum
                    0x03F8,             // Range Maximum
                    0x00,               // Alignment
                    0x08,               // Length
                    )
                IRQNoFlags ()
                    {4}
            })
        }

        Device (GED)
        {
            Name (_HID, "ACPI0013" /* Generic Event Device */)  // _HID: Hardware ID
            Name (_UID, "GED")  // _UID: Unique ID
            Name (_CRS, ResourceTemplate ()  // _CRS: Current Resource Settings
            {
                Interrupt (ResourceConsumer, Edge, ActiveHigh, Exclusive, ,, )
                {
                    0x00000009,
                }
            })
            OperationRegion (EREG, SystemMemory, 0xFEA00000, 0x04)
            Field (EREG, DWordAcc, NoLock, WriteAsZeros)
            {
                ESEL,   32
            }

            Method (_EVT, 1, Serialized)  // _EVT: Event
            {
                Local0 = ESEL /* \_SB_.GED_.ESEL */
                If (((Local0 & 0x02) == 0x02))
                {
                    Notify (PWRB, 0x80) // Status Change
                }
            }
        }

        Device (PWRB)
        {
            Name (_HID, "PNP0C0C" /* Power Button Device */)  // _HID: Hardware ID
            Name (_UID, Zero)  // _UID: Unique ID
        }

        Device (VR07)
        {
            Name (_HID, "LNRO0005")  // _HID: Hardware ID
            Name (_UID, 0x07)  // _UID: Unique ID
            Name (_CCA, One)  // _CCA: Cache Coherency Attribute
            Name (_CRS, ResourceTemplate ()  // _CRS: Current Resource Settings
            {
                Memory32Fixed (ReadWrite,
                    0xFEB00E00,         // Address Base
                    0x00000200,         // Address Length
                    )
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive, ,, )
                {
                    0x00000017,
                }
            })
        }
    }

    Scope (\)
    {
        Name (_S5, Package (0x04)  // _S5_: S5 System State
        {
            0x05, 
            Zero, 
            Zero, 
            Zero
        })
    }
}

