/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/****************************************************************
 * CPU hotplug
 ****************************************************************/
#define CPU_HOTPLUG_RESOURCE_DEVICE PRES

Scope(\_SB) {
    /* Objects filled in by run-time generated SSDT */
    External(NTFY, MethodObj)
    External(CPON, PkgObj)

    /* Methods called by run-time generated SSDT Processor objects */
    Method(CPMA, 1, NotSerialized) {
        // _MAT method - create an madt apic buffer
        // Arg0 = Processor ID = Local APIC ID
        // Local0 = CPON flag for this cpu
        Store(DerefOf(Index(CPON, Arg0)), Local0)
        // Local1 = Buffer (in madt apic form) to return
        Store(Buffer(8) {0x00, 0x08, 0x00, 0x00, 0x00, 0, 0, 0}, Local1)
        // Update the processor id, lapic id, and enable/disable status
        Store(Arg0, Index(Local1, 2))
        Store(Arg0, Index(Local1, 3))
        Store(Local0, Index(Local1, 4))
        Return (Local1)
    }
    Method(CPST, 1, NotSerialized) {
        // _STA method - return ON status of cpu
        // Arg0 = Processor ID = Local APIC ID
        // Local0 = CPON flag for this cpu
        Store(DerefOf(Index(CPON, Arg0)), Local0)
        If (Local0) {
            Return (0xF)
        } Else {
            Return (0x0)
        }
    }
    Method(CPEJ, 2, NotSerialized) {
        // _EJ0 method - eject callback
        Sleep(200)
    }

#define CPU_STATUS_LEN ACPI_GPE_PROC_LEN
    OperationRegion(PRST, SystemIO, CPU_STATUS_BASE, CPU_STATUS_LEN)
    Field(PRST, ByteAcc, NoLock, Preserve) {
        PRS, 256
    }
    Method(PRSC, 0) {
        // Local5 = active cpu bitmap
        Store(PRS, Local5)
        // Local2 = last read byte from bitmap
        Store(Zero, Local2)
        // Local0 = Processor ID / APIC ID iterator
        Store(Zero, Local0)
        While (LLess(Local0, SizeOf(CPON))) {
            // Local1 = CPON flag for this cpu
            Store(DerefOf(Index(CPON, Local0)), Local1)
            If (And(Local0, 0x07)) {
                // Shift down previously read bitmap byte
                ShiftRight(Local2, 1, Local2)
            } Else {
                // Read next byte from cpu bitmap
                Store(DerefOf(Index(Local5, ShiftRight(Local0, 3))), Local2)
            }
            // Local3 = active state for this cpu
            Store(And(Local2, 1), Local3)

            If (LNotEqual(Local1, Local3)) {
                // State change - update CPON with new state
                Store(Local3, Index(CPON, Local0))
                // Do CPU notify
                If (LEqual(Local3, 1)) {
                    NTFY(Local0, 1)
                } Else {
                    NTFY(Local0, 3)
                }
            }
            Increment(Local0)
        }
    }

    Device(CPU_HOTPLUG_RESOURCE_DEVICE) {
        Name(_HID, EisaId("PNP0A06"))

        Name(_CRS, ResourceTemplate() {
            IO(Decode16, CPU_STATUS_BASE, CPU_STATUS_BASE, 0, CPU_STATUS_LEN)
        })

        Name(_STA, 0xB) /* present, functioning, decoding, not shown in UI */
    }
}
