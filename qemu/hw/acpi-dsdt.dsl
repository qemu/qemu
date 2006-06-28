/*
 * QEMU ACPI DSDT ASL definition
 * 
 * Copyright (c) 2006 Fabrice Bellard
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
DefinitionBlock (
    "acpi-dsdt.aml",    // Output Filename
    "DSDT",             // Signature
    0x01,               // DSDT Compliance Revision
    "QEMU",             // OEMID
    "QEMUDSDT",         // TABLE ID
    0x1                 // OEM Revision
    )
{
    Scope (\)
    {
        /* CMOS memory access */
        OperationRegion (CMS, SystemIO, 0x70, 0x02)
        Field (CMS, ByteAcc, NoLock, Preserve)
        {
            CMSI,   8, 
            CMSD,   8
        }
        Method (CMRD, 1, NotSerialized)
        {
            Store (Arg0, CMSI)
            Store (CMSD, Local0)
            Return (Local0)
        }

        /* Debug Output */
        OperationRegion (DBG, SystemIO, 0xb044, 0x04)
        Field (DBG, DWordAcc, NoLock, Preserve)
        {
            DBGL,   32, 
        }
    }


    /* PCI Bus definition */
    Scope(\_SB) {
        Device(PCI0) {
            Name (_HID, EisaId ("PNP0A03"))
            Name (_ADR, 0x00)
            Name (_UID, 1)
            Name(_PRT, Package() {
                /* PCI IRQ routing table, example from ACPI 2.0a specification,
                   section 6.2.8.1 */
                /* Note: we provide the same info as the PCI routing
                   table of the Bochs BIOS */
                   
                // PCI Slot 0
                Package() {0x0000ffff, 0, LNKD, 0}, 
                Package() {0x0000ffff, 1, LNKA, 0}, 
                Package() {0x0000ffff, 2, LNKB, 0}, 
                Package() {0x0000ffff, 3, LNKC, 0}, 

                // PCI Slot 1
                Package() {0x0001ffff, 0, LNKA, 0}, 
                Package() {0x0001ffff, 1, LNKB, 0}, 
                Package() {0x0001ffff, 2, LNKC, 0}, 
                Package() {0x0001ffff, 3, LNKD, 0}, 
                
                // PCI Slot 2
                Package() {0x0002ffff, 0, LNKB, 0}, 
                Package() {0x0002ffff, 1, LNKC, 0}, 
                Package() {0x0002ffff, 2, LNKD, 0}, 
                Package() {0x0002ffff, 3, LNKA, 0}, 

                // PCI Slot 3
                Package() {0x0003ffff, 0, LNKC, 0}, 
                Package() {0x0003ffff, 1, LNKD, 0}, 
                Package() {0x0003ffff, 2, LNKA, 0}, 
                Package() {0x0003ffff, 3, LNKB, 0}, 

                // PCI Slot 4
                Package() {0x0004ffff, 0, LNKD, 0}, 
                Package() {0x0004ffff, 1, LNKA, 0}, 
                Package() {0x0004ffff, 2, LNKB, 0}, 
                Package() {0x0004ffff, 3, LNKC, 0}, 

                // PCI Slot 5
                Package() {0x0005ffff, 0, LNKA, 0}, 
                Package() {0x0005ffff, 1, LNKB, 0}, 
                Package() {0x0005ffff, 2, LNKC, 0}, 
                Package() {0x0005ffff, 3, LNKD, 0}, 
            })

            Method (_CRS, 0, NotSerialized)
            {
            Name (MEMP, ResourceTemplate ()
            {
                WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    0x0000,             // Address Space Granularity
                    0x0000,             // Address Range Minimum
                    0x00FF,             // Address Range Maximum
                    0x0000,             // Address Translation Offset
                    0x0100,             // Address Length
                    ,, )
                IO (Decode16,
                    0x0CF8,             // Address Range Minimum
                    0x0CF8,             // Address Range Maximum
                    0x01,               // Address Alignment
                    0x08,               // Address Length
                    )
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
                    0x0000,             // Address Space Granularity
                    0x0000,             // Address Range Minimum
                    0x0CF7,             // Address Range Maximum
                    0x0000,             // Address Translation Offset
                    0x0CF8,             // Address Length
                    ,, , TypeStatic)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
                    0x0000,             // Address Space Granularity
                    0x0D00,             // Address Range Minimum
                    0xFFFF,             // Address Range Maximum
                    0x0000,             // Address Translation Offset
                    0xF300,             // Address Length
                    ,, , TypeStatic)
                DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
                    0x00000000,         // Address Space Granularity
                    0x000A0000,         // Address Range Minimum
                    0x000BFFFF,         // Address Range Maximum
                    0x00000000,         // Address Translation Offset
                    0x00020000,         // Address Length
                    ,, , AddressRangeMemory, TypeStatic)
                DWordMemory (ResourceProducer, PosDecode, MinNotFixed, MaxFixed, NonCacheable, ReadWrite,
                    0x00000000,         // Address Space Granularity
                    0x00000000,         // Address Range Minimum
                    0xFEBFFFFF,         // Address Range Maximum
                    0x00000000,         // Address Translation Offset
                    0x00000000,         // Address Length
                    ,, MEMF, AddressRangeMemory, TypeStatic)
            })
                CreateDWordField (MEMP, \_SB.PCI0._CRS.MEMF._MIN, PMIN)
                CreateDWordField (MEMP, \_SB.PCI0._CRS.MEMF._MAX, PMAX)
                CreateDWordField (MEMP, \_SB.PCI0._CRS.MEMF._LEN, PLEN)
                /* compute available RAM */
                Add(CMRD(0x34), ShiftLeft(CMRD(0x35), 8), Local0)
                ShiftLeft(Local0, 16, Local0)
                Add(Local0, 0x1000000, Local0)
                /* update field of last region */
                Store(Local0, PMIN)
                Subtract (PMAX, PMIN, PLEN)
                Increment (PLEN)
                Return (MEMP)
            }
        }
    }

    Scope(\_SB.PCI0) {

	/* PIIX3 ISA bridge */
        Device (ISA) {
            Name (_ADR, 0x00010000)
        
            /* PIIX PCI to ISA irq remapping */
            OperationRegion (P40C, PCI_Config, 0x60, 0x04)


            /* Keyboard seems to be important for WinXP install */
            Device (KBD)
            {
                Name (_HID, EisaId ("PNP0303"))
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0f)
                }

                Method (_CRS, 0, NotSerialized)
                {
                     Name (TMP, ResourceTemplate ()
                     {
                    IO (Decode16,
                        0x0060,             // Address Range Minimum
                        0x0060,             // Address Range Maximum
                        0x01,               // Address Alignment
                        0x01,               // Address Length
                        )
                    IO (Decode16,
                        0x0064,             // Address Range Minimum
                        0x0064,             // Address Range Maximum
                        0x01,               // Address Alignment
                        0x01,               // Address Length
                        )
                    IRQNoFlags ()
                        {1}
                    })
                    Return (TMP)
                }
            }

	    /* PS/2 mouse */
            Device (MOU) 
            {
                Name (_HID, EisaId ("PNP0F13"))
                Method (_STA, 0, NotSerialized)
                {
                    Return (0x0f)
                }

                Method (_CRS, 0, NotSerialized)
                {
                    Name (TMP, ResourceTemplate ()
                    {
                         IRQNoFlags () {12}
                    })
                    Return (TMP)
                }
            }

	    /* PS/2 floppy controller */
	    Device (FDC0)
	    {
	        Name (_HID, EisaId ("PNP0700"))
		Method (_STA, 0, NotSerialized)
		{
		    Return (0x0F)
		}
		Method (_CRS, 0, NotSerialized)
		{
		    Name (BUF0, ResourceTemplate ()
                    {
                        IO (Decode16, 0x03F2, 0x03F2, 0x00, 0x04)
                        IO (Decode16, 0x03F7, 0x03F7, 0x00, 0x01)
                        IRQNoFlags () {6}
                        DMA (Compatibility, NotBusMaster, Transfer8) {2}
                    })
		    Return (BUF0)
		}
	    }

	    /* Parallel port */
	    Device (LPT)
	    {
	        Name (_HID, EisaId ("PNP0400"))
		Method (_STA, 0, NotSerialized)
		{
		    Store (\_SB.PCI0.PX13.DRSA, Local0)
		    And (Local0, 0x80000000, Local0)
		    If (LEqual (Local0, 0))
		    {
			Return (0x00)
		    }
		    Else
		    {
			Return (0x0F)
		    }
		}
		Method (_CRS, 0, NotSerialized)
		{
		    Name (BUF0, ResourceTemplate ()
                    {
			IO (Decode16, 0x0378, 0x0378, 0x08, 0x08)
			IRQNoFlags () {7}
		    })
		    Return (BUF0)
		}
	    }

	    /* Serial Ports */
	    Device (COM1)
	    {
	        Name (_HID, EisaId ("PNP0501"))
		Name (_UID, 0x01)
		Method (_STA, 0, NotSerialized)
		{
		    Store (\_SB.PCI0.PX13.DRSC, Local0)
		    And (Local0, 0x08000000, Local0)
		    If (LEqual (Local0, 0))
		    {
			Return (0x00)
		    }
		    Else
		    {
			Return (0x0F)
		    }
		}
		Method (_CRS, 0, NotSerialized)
		{
		    Name (BUF0, ResourceTemplate ()
                    {
			IO (Decode16, 0x03F8, 0x03F8, 0x00, 0x08)
                	IRQNoFlags () {4}
		    })
		    Return (BUF0)
		}
	    }

	    Device (COM2)
	    {
	        Name (_HID, EisaId ("PNP0501"))
		Name (_UID, 0x02)
		Method (_STA, 0, NotSerialized)
		{
		    Store (\_SB.PCI0.PX13.DRSC, Local0)
		    And (Local0, 0x80000000, Local0)
		    If (LEqual (Local0, 0))
		    {
			Return (0x00)
		    }
		    Else
		    {
			Return (0x0F)
		    }
		}
		Method (_CRS, 0, NotSerialized)
		{
		    Name (BUF0, ResourceTemplate ()
                    {
			IO (Decode16, 0x02F8, 0x02F8, 0x00, 0x08)
                	IRQNoFlags () {3}
		    })
		    Return (BUF0)
		}
	    }
        }

	/* PIIX4 PM */
        Device (PX13) {
	    Name (_ADR, 0x00010003)

	    OperationRegion (P13C, PCI_Config, 0x5c, 0x24)
	    Field (P13C, DWordAcc, NoLock, Preserve)
	    {
		DRSA, 32,
		DRSB, 32,
		DRSC, 32,
		DRSE, 32,
		DRSF, 32,
		DRSG, 32,
		DRSH, 32,
		DRSI, 32,
		DRSJ, 32
	    }
	}
    }

    /* PCI IRQs */
    Scope(\_SB) {
         Field (\_SB.PCI0.ISA.P40C, ByteAcc, NoLock, Preserve)
         {
             PRQ0,   8, 
             PRQ1,   8, 
             PRQ2,   8, 
             PRQ3,   8
         }

        Device(LNKA){
                Name(_HID, EISAID("PNP0C0F"))     // PCI interrupt link
                Name(_UID, 1)
                Name(_PRS, ResourceTemplate(){
                    IRQ (Level, ActiveLow, Shared)
                        {3,4,5,6,7,9,10,11,12}
                })
                Method (_STA, 0, NotSerialized)
                {
                    Store (0x0B, Local0)
                    If (And (0x80, PRQ0, Local1))
                    {
                         Store (0x09, Local0)
                    }
                    Return (Local0)
                }
                Method (_DIS, 0, NotSerialized)
                {
                    Or (PRQ0, 0x80, PRQ0)
                }
                Method (_CRS, 0, NotSerialized)
                {
                    Name (PRR0, ResourceTemplate ()
                    {
                        IRQ (Level, ActiveLow, Shared)
                            {1}
                    })
                    CreateWordField (PRR0, 0x01, TMP)
                    Store (PRQ0, Local0)
                    If (LLess (Local0, 0x80))
                    {
                        ShiftLeft (One, Local0, TMP)
                    }
                    Else
                    {
                        Store (Zero, TMP)
                    }
                    Return (PRR0)
                }
                Method (_SRS, 1, NotSerialized)
                {
                    CreateWordField (Arg0, 0x01, TMP)
                    FindSetRightBit (TMP, Local0)
                    Decrement (Local0)
                    Store (Local0, PRQ0)
                }
        }
        Device(LNKB){
                Name(_HID, EISAID("PNP0C0F"))     // PCI interrupt link
                Name(_UID, 2)
                Name(_PRS, ResourceTemplate(){
                    IRQ (Level, ActiveLow, Shared)
                        {3,4,5,6,7,9,10,11,12}
                })
                Method (_STA, 0, NotSerialized)
                {
                    Store (0x0B, Local0)
                    If (And (0x80, PRQ1, Local1))
                    {
                         Store (0x09, Local0)
                    }
                    Return (Local0)
                }
                Method (_DIS, 0, NotSerialized)
                {
                    Or (PRQ1, 0x80, PRQ1)
                }
                Method (_CRS, 0, NotSerialized)
                {
                    Name (PRR0, ResourceTemplate ()
                    {
                        IRQ (Level, ActiveLow, Shared)
                            {1}
                    })
                    CreateWordField (PRR0, 0x01, TMP)
                    Store (PRQ1, Local0)
                    If (LLess (Local0, 0x80))
                    {
                        ShiftLeft (One, Local0, TMP)
                    }
                    Else
                    {
                        Store (Zero, TMP)
                    }
                    Return (PRR0)
                }
                Method (_SRS, 1, NotSerialized)
                {
                    CreateWordField (Arg0, 0x01, TMP)
                    FindSetRightBit (TMP, Local0)
                    Decrement (Local0)
                    Store (Local0, PRQ1)
                }
        }
        Device(LNKC){
                Name(_HID, EISAID("PNP0C0F"))     // PCI interrupt link
                Name(_UID, 3)
                Name(_PRS, ResourceTemplate(){
                    IRQ (Level, ActiveLow, Shared)
                        {3,4,5,6,7,9,10,11,12}
                })
                Method (_STA, 0, NotSerialized)
                {
                    Store (0x0B, Local0)
                    If (And (0x80, PRQ2, Local1))
                    {
                         Store (0x09, Local0)
                    }
                    Return (Local0)
                }
                Method (_DIS, 0, NotSerialized)
                {
                    Or (PRQ2, 0x80, PRQ2)
                }
                Method (_CRS, 0, NotSerialized)
                {
                    Name (PRR0, ResourceTemplate ()
                    {
                        IRQ (Level, ActiveLow, Shared)
                            {1}
                    })
                    CreateWordField (PRR0, 0x01, TMP)
                    Store (PRQ2, Local0)
                    If (LLess (Local0, 0x80))
                    {
                        ShiftLeft (One, Local0, TMP)
                    }
                    Else
                    {
                        Store (Zero, TMP)
                    }
                    Return (PRR0)
                }
                Method (_SRS, 1, NotSerialized)
                {
                    CreateWordField (Arg0, 0x01, TMP)
                    FindSetRightBit (TMP, Local0)
                    Decrement (Local0)
                    Store (Local0, PRQ2)
                }
        }
        Device(LNKD){
                Name(_HID, EISAID("PNP0C0F"))     // PCI interrupt link
                Name(_UID, 4)
                Name(_PRS, ResourceTemplate(){
                    IRQ (Level, ActiveLow, Shared)
                        {3,4,5,6,7,9,10,11,12}
                })
                Method (_STA, 0, NotSerialized)
                {
                    Store (0x0B, Local0)
                    If (And (0x80, PRQ3, Local1))
                    {
                         Store (0x09, Local0)
                    }
                    Return (Local0)
                }
                Method (_DIS, 0, NotSerialized)
                {
                    Or (PRQ3, 0x80, PRQ3)
                }
                Method (_CRS, 0, NotSerialized)
                {
                    Name (PRR0, ResourceTemplate ()
                    {
                        IRQ (Level, ActiveLow, Shared)
                            {1}
                    })
                    CreateWordField (PRR0, 0x01, TMP)
                    Store (PRQ3, Local0)
                    If (LLess (Local0, 0x80))
                    {
                        ShiftLeft (One, Local0, TMP)
                    }
                    Else
                    {
                        Store (Zero, TMP)
                    }
                    Return (PRR0)
                }
                Method (_SRS, 1, NotSerialized)
                {
                    CreateWordField (Arg0, 0x01, TMP)
                    FindSetRightBit (TMP, Local0)
                    Decrement (Local0)
                    Store (Local0, PRQ3)
                }
        }
    }

    /* S5 = power off state */
    Name (_S5, Package (4) {
        0x00, // PM1a_CNT.SLP_TYP 
        0x00, // PM2a_CNT.SLP_TYP 
        0x00, // reserved
        0x00, // reserved
    })
}
