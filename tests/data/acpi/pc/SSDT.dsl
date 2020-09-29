/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20190509 (64-bit version)
 * Copyright (c) 2000 - 2019 Intel Corporation
 * 
 * Disassembling to symbolic ASL+ operators
 *
 * Disassembly of tests/data/acpi/pc/SSDT.dimmpxm, Mon Sep 28 17:24:38 2020
 *
 * Original Table Header:
 *     Signature        "SSDT"
 *     Length           0x000002DE (734)
 *     Revision         0x01
 *     Checksum         0x56
 *     OEM ID           "BOCHS "
 *     OEM Table ID     "NVDIMM"
 *     OEM Revision     0x00000001 (1)
 *     Compiler ID      "BXPC"
 *     Compiler Version 0x00000001 (1)
 */
DefinitionBlock ("", "SSDT", 1, "BOCHS ", "NVDIMM", 0x00000001)
{
    Scope (\_SB)
    {
        Device (NVDR)
        {
            Name (_HID, "ACPI0012" /* NVDIMM Root Device */)  // _HID: Hardware ID
            Method (NCAL, 5, Serialized)
            {
                Local6 = MEMA /* \MEMA */
                OperationRegion (NPIO, SystemIO, 0x0A18, 0x04)
                OperationRegion (NRAM, SystemMemory, Local6, 0x1000)
                Field (NPIO, DWordAcc, NoLock, Preserve)
                {
                    NTFI,   32
                }

                Field (NRAM, DWordAcc, NoLock, Preserve)
                {
                    HDLE,   32, 
                    REVS,   32, 
                    FUNC,   32, 
                    FARG,   32672
                }

                Field (NRAM, DWordAcc, NoLock, Preserve)
                {
                    RLEN,   32, 
                    ODAT,   32736
                }

                If ((Arg4 == Zero))
                {
                    Local0 = ToUUID ("2f10e7a4-9e91-11e4-89d3-123b93f75cba")
                }
                ElseIf ((Arg4 == 0x00010000))
                {
                    Local0 = ToUUID ("648b9cf2-cda1-4312-8ad9-49c4af32bd62")
                }
                Else
                {
                    Local0 = ToUUID ("4309ac30-0d11-11e4-9191-0800200c9a66")
                }

                If (((Local6 == Zero) | (Arg0 != Local0)))
                {
                    If ((Arg2 == Zero))
                    {
                        Return (Buffer (One)
                        {
                             0x00                                             // .
                        })
                    }

                    Return (Buffer (One)
                    {
                         0x01                                             // .
                    })
                }

                HDLE = Arg4
                REVS = Arg1
                FUNC = Arg2
                If (((ObjectType (Arg3) == 0x04) & (SizeOf (Arg3) == One)))
                {
                    Local2 = Arg3 [Zero]
                    Local3 = DerefOf (Local2)
                    FARG = Local3
                }

                NTFI = Local6
                Local1 = (RLEN - 0x04)
                If ((Local1 < 0x08))
                {
                    Local2 = Zero
                    Name (TBUF, Buffer (One)
                    {
                         0x00                                             // .
                    })
                    Local7 = Buffer (Zero){}
                    While ((Local2 < Local1))
                    {
                        TBUF [Zero] = DerefOf (ODAT [Local2])
                        Concatenate (Local7, TBUF, Local7)
                        Local2++
                    }

                    Return (Local7)
                }

                Local1 = (Local1 << 0x03)
                CreateField (ODAT, Zero, Local1, OBUF)
                Return (OBUF) /* \_SB_.NVDR.NCAL.OBUF */
            }

            Method (_DSM, 4, NotSerialized)  // _DSM: Device-Specific Method
            {
                Return (NCAL (Arg0, Arg1, Arg2, Arg3, Zero))
            }

            Name (RSTA, Zero)
            Method (RFIT, 1, Serialized)
            {
                Name (OFST, Zero)
                OFST = Arg0
                Local0 = NCAL (ToUUID ("648b9cf2-cda1-4312-8ad9-49c4af32bd62"), One, One, Package (0x01)
                        {
                            OFST
                        }, 0x00010000)
                CreateDWordField (Local0, Zero, STAU)
                RSTA = STAU /* \_SB_.NVDR.RFIT.STAU */
                If ((Zero != STAU))
                {
                    Return (Buffer (Zero){})
                }

                Local1 = SizeOf (Local0)
                Local1 -= 0x04
                If ((Local1 == Zero))
                {
                    Return (Buffer (Zero){})
                }

                CreateField (Local0, 0x20, (Local1 << 0x03), BUFF)
                Return (BUFF) /* \_SB_.NVDR.RFIT.BUFF */
            }

            Method (_FIT, 0, Serialized)  // _FIT: Firmware Interface Table
            {
                Local2 = Buffer (Zero){}
                Local3 = Zero
                While (One)
                {
                    Local0 = RFIT (Local3)
                    Local1 = SizeOf (Local0)
                    If ((RSTA == 0x0100))
                    {
                        Local2 = Buffer (Zero){}
                        Local3 = Zero
                    }
                    Else
                    {
                        If ((Local1 == Zero))
                        {
                            Return (Local2)
                        }

                        Local3 += Local1
                        Concatenate (Local2, Local0, Local2)
                    }
                }
            }

            Device (NV00)
            {
                Name (_ADR, One)  // _ADR: Address
                Method (_DSM, 4, NotSerialized)  // _DSM: Device-Specific Method
                {
                    Return (NCAL (Arg0, Arg1, Arg2, Arg3, One))
                }
            }

            Device (NV01)
            {
                Name (_ADR, 0x02)  // _ADR: Address
                Method (_DSM, 4, NotSerialized)  // _DSM: Device-Specific Method
                {
                    Return (NCAL (Arg0, Arg1, Arg2, Arg3, 0x02))
                }
            }

            Device (NV02)
            {
                Name (_ADR, 0x03)  // _ADR: Address
                Method (_DSM, 4, NotSerialized)  // _DSM: Device-Specific Method
                {
                    Return (NCAL (Arg0, Arg1, Arg2, Arg3, 0x03))
                }
            }
        }
    }

    Name (MEMA, 0x07FFE000)
}

