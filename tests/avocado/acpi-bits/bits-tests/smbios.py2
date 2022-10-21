# Copyright (c) 2015, Intel Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#     * Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright notice,
#       this list of conditions and the following disclaimer in the documentation
#       and/or other materials provided with the distribution.
#     * Neither the name of Intel Corporation nor the names of its contributors
#       may be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
# ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""SMBIOS/DMI module."""

import bits
import bitfields
import ctypes
import redirect
import struct
import uuid
import unpack
import ttypager
import sys

class SMBIOS(unpack.Struct):
    def __new__(cls):
        if sys.platform == "BITS-EFI":
            import efi
            sm_ptr = efi.system_table.ConfigurationTableDict.get(efi.SMBIOS_TABLE_GUID)
        else:
            address = 0xF0000
            mem = bits.memory(0xF0000, 0x10000)
            for offset in range(0, len(mem), 16):
                signature = (ctypes.c_char * 4).from_address(address + offset).value
                if signature == "_SM_":
                    entry_point_length = ctypes.c_ubyte.from_address(address + offset + 5).value
                    csum = sum(map(ord, mem[offset:offset + entry_point_length])) & 0xff
                    if csum == 0:
                        sm_ptr = address + offset
                        break
            else:
                return None

        if not sm_ptr:
            return None

        sm = super(SMBIOS, cls).__new__(cls)
        sm._header_memory = bits.memory(sm_ptr, 0x1f)
        return sm

    def __init__(self):
        super(SMBIOS, self).__init__()
        u = unpack.Unpackable(self._header_memory)
        self.add_field('header', Header(u))
        self._structure_memory = bits.memory(self.header.structure_table_address, self.header.structure_table_length)
        u = unpack.Unpackable(self._structure_memory)
        self.add_field('structures', unpack.unpack_all(u, _smbios_structures, self), unpack.format_each("\n\n{!r}"))

    def structure_type(self, num):
        '''Dumps structure of given Type if present'''
        try:
            types_present = [self.structures[x].smbios_structure_type for x in range(len(self.structures))]
            matrix = dict()
            for index in range(len(types_present)):
                if types_present.count(types_present[index]) == 1:
                    matrix[types_present[index]] = self.structures[index]
                else: # if multiple structures of the same type, return a list of structures for the type number
                    if matrix.has_key(types_present[index]):
                        matrix[types_present[index]].append(self.structures[index])
                    else:
                        matrix[types_present[index]] = [self.structures[index]]
            return matrix[num]
        except:
            print "Failure: Type {} - not found".format(num)

class Header(unpack.Struct):
    def __new__(cls, u):
        return super(Header, cls).__new__(cls)

    def __init__(self, u):
        super(Header, self).__init__()
        self.raw_data = u.unpack_rest()
        u = unpack.Unpackable(self.raw_data)
        self.add_field('anchor_string', u.unpack_one("4s"))
        self.add_field('checksum', u.unpack_one("B"))
        self.add_field('length', u.unpack_one("B"))
        self.add_field('major_version', u.unpack_one("B"))
        self.add_field('minor_version', u.unpack_one("B"))
        self.add_field('max_structure_size', u.unpack_one("<H"))
        self.add_field('entry_point_revision', u.unpack_one("B"))
        self.add_field('formatted_area', u.unpack_one("5s"))
        self.add_field('intermediate_anchor_string', u.unpack_one("5s"))
        self.add_field('intermediate_checksum', u.unpack_one("B"))
        self.add_field('structure_table_length', u.unpack_one("<H"))
        self.add_field('structure_table_address', u.unpack_one("<I"))
        self.add_field('number_structures', u.unpack_one("<H"))
        self.add_field('bcd_revision', u.unpack_one("B"))
        if not u.at_end():
            self.add_field('data', u.unpack_rest())

class SmbiosBaseStructure(unpack.Struct):
    def __new__(cls, u, sm):
        t = u.unpack_peek_one("B")
        if cls.smbios_structure_type is not None and t != cls.smbios_structure_type:
            return None
        return super(SmbiosBaseStructure, cls).__new__(cls)

    def __init__(self, u, sm):
        super(SmbiosBaseStructure, self).__init__()
        self.start_offset = u.offset
        length = u.unpack_peek_one("<xB")
        self.raw_data = u.unpack_raw(length)
        self.u = unpack.Unpackable(self.raw_data)

        self.strings_offset = u.offset
        def unpack_string():
            return "".join(iter(lambda: u.unpack_one("c"), "\x00"))
        strings = list(iter(unpack_string, ""))
        if not strings:
            u.skip(1)

        self.strings_length = u.offset - self.strings_offset
        self.raw_strings = str(bits.memory(sm.header.structure_table_address + self.strings_offset, self.strings_length))

        if len(strings):
            self.strings = strings

        self.add_field('type', self.u.unpack_one("B"))
        self.add_field('length', self.u.unpack_one("B"))
        self.add_field('handle', self.u.unpack_one("<H"))

    def fini(self):
        if not self.u.at_end():
            self.add_field('data', self.u.unpack_rest())
        del self.u

    def fmtstr(self, i):
        """Format the specified index and the associated string"""
        return "{} '{}'".format(i, self.getstr(i))

    def getstr(self, i):
        """Get the string associated with the given index"""
        if i == 0:
            return "(none)"
        if not hasattr(self, "strings"):
            return "(error: structure has no strings)"
        if i > len(self.strings):
            return "(error: string index out of range)"
        return self.strings[i - 1]

class BIOSInformation(SmbiosBaseStructure):
    smbios_structure_type = 0

    def __init__(self, u, sm):
        super(BIOSInformation, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('vendor', u.unpack_one("B"), self.fmtstr)
            self.add_field('version', u.unpack_one("B"), self.fmtstr)
            self.add_field('starting_address_segment', u.unpack_one("<H"))
            self.add_field('release_date', u.unpack_one("B"), self.fmtstr)
            self.add_field('rom_size', u.unpack_one("B"))
            self.add_field('characteristics', u.unpack_one("<Q"))
            minor_version_str = str(sm.header.minor_version) # 34 is .34, 4 is .4, 41 is .41; compare ASCIIbetically to compare initial digits rather than numeric value
            if (sm.header.major_version, minor_version_str) >= (2,"4"):
                characteristic_bytes = 2
            else:
                characteristic_bytes = self.length - 0x12
            self.add_field('characteristics_extensions', [u.unpack_one("B") for b in range(characteristic_bytes)])
            if (sm.header.major_version, minor_version_str) >= (2,"4"):
                self.add_field('major_release', u.unpack_one("B"))
                self.add_field('minor_release', u.unpack_one("B"))
                self.add_field('ec_major_release', u.unpack_one("B"))
                self.add_field('ec_minor_release', u.unpack_one("B"))
        except:
            self.decode_failure = True
            print "Error parsing BIOSInformation"
            import traceback
            traceback.print_exc()
        self.fini()

class SystemInformation(SmbiosBaseStructure):
    smbios_structure_type = 1

    def __init__(self, u, sm):
        super(SystemInformation, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('manufacturer', u.unpack_one("B"), self.fmtstr)
            self.add_field('product_name', u.unpack_one("B"), self.fmtstr)
            self.add_field('version', u.unpack_one("B"), self.fmtstr)
            self.add_field('serial_number', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x8:
                self.add_field('uuid', uuid.UUID(bytes_le=u.unpack_one("16s")))
                wakeup_types = {
                    0: 'Reserved',
                    1: 'Other',
                    2: 'Unknown',
                    3: 'APM Timer',
                    4: 'Modem Ring',
                    5: 'LAN Remote',
                    6: 'Power Switch',
                    7: 'PCI PME#',
                    8: 'AC Power Restored'
                }
                self.add_field('wakeup_type', u.unpack_one("B"), unpack.format_table("{}", wakeup_types))
            if self.length > 0x19:
                self.add_field('sku_number', u.unpack_one("B"), self.fmtstr)
                self.add_field('family', u.unpack_one("B"), self.fmtstr)
        except:
            self.decode_failure = True
            print "Error parsing SystemInformation"
            import traceback
            traceback.print_exc()
        self.fini()

_board_types = {
    1: 'Unknown',
    2: 'Other',
    3: 'Server Blade',
    4: 'Connectivity Switch',
    5: 'System Management Module',
    6: 'Processor Module',
    7: 'I/O Module',
    8: 'Memory Module',
    9: 'Daughter Board',
    0xA: 'Motherboard',
    0xB: 'Processor/Memory Module',
    0xC: 'Processor/IO Module',
    0xD: 'Interconnect Board'
}

class BaseboardInformation(SmbiosBaseStructure):
    smbios_structure_type = 2

    def __init__(self, u, sm):
        super(BaseboardInformation, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('manufacturer', u.unpack_one("B"), self.fmtstr)
            self.add_field('product', u.unpack_one("B"), self.fmtstr)
            self.add_field('version', u.unpack_one("B"), self.fmtstr)
            self.add_field('serial_number', u.unpack_one("B"), self.fmtstr)

            if self.length > 0x8:
                self.add_field('asset_tag', u.unpack_one("B"), self.fmtstr)

            if self.length > 0x9:
                self.add_field('feature_flags', u.unpack_one("B"))
                self.add_field('hosting_board', bool(bitfields.getbits(self.feature_flags, 0)), "feature_flags[0]={}")
                self.add_field('requires_daughter_card', bool(bitfields.getbits(self.feature_flags, 1)), "feature_flags[1]={}")
                self.add_field('removable', bool(bitfields.getbits(self.feature_flags, 2)), "feature_flags[2]={}")
                self.add_field('replaceable', bool(bitfields.getbits(self.feature_flags, 3)), "feature_flags[3]={}")
                self.add_field('hot_swappable', bool(bitfields.getbits(self.feature_flags, 4)), "feature_flags[4]={}")

            if self.length > 0xA:
                self.add_field('location', u.unpack_one("B"), self.fmtstr)

            if self.length > 0xB:
                self.add_field('chassis_handle', u.unpack_one("<H"))

            if self.length > 0xD:
                self.add_field('board_type', u.unpack_one("B"), unpack.format_table("{}", _board_types))

            if self.length > 0xE:
                self.add_field('handle_count', u.unpack_one("B"))
                if self.handle_count > 0:
                    self.add_field('contained_object_handles', tuple(u.unpack_one("<H") for i in range(self.handle_count)))
        except:
            self.decode_failure = True
            print "Error parsing BaseboardInformation"
            import traceback
            traceback.print_exc()
        self.fini()

class SystemEnclosure(SmbiosBaseStructure):
    smbios_structure_type = 3

    def __init__(self, u, sm):
        super(SystemEnclosure, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('manufacturer', u.unpack_one("B"), self.fmtstr)
            self.add_field('enumerated_type', u.unpack_one("B"))
            self.add_field('chassis_lock_present', bool(bitfields.getbits(self.enumerated_type, 7)), "enumerated_type[7]={}")
            board_types = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: 'Desktop',
                0x04: 'Low Profile Desktop',
                0x05: 'Pizza Box',
                0x06: 'Mini Tower',
                0x07: 'Tower',
                0x08: 'Portable',
                0x09: 'Laptop',
                0x0A: 'Notebook',
                0x0B: 'Hand Held',
                0x0C: 'Docking Station',
                0x0D: 'All in One',
                0x0E: 'Sub Notebook',
                0x0F: 'Space-saving',
                0x10: 'Lunch Box',
                0x11: 'Main Server Chassis',
                0x12: 'Expansion Chassis',
                0x13: 'SubChassis',
                0x14: 'Bus Expansion Chassis',
                0x15: 'Peripheral Chassis',
                0x16: 'RAID Chassis',
                0x17: 'Rack Mount Chassis',
                0x18: 'Sealed-case PC',
                0x19: 'Multi-system chassis W',
                0x1A: 'Compact PCI',
                0x1B: 'Advanced TCA',
                0x1C: 'Blade',
                0x1D: 'Blade Enclosure',
            }
            self.add_field('system_enclosure_type', bitfields.getbits(self.enumerated_type, 6, 0), unpack.format_table("enumerated_type[6:0]={}", board_types))
            self.add_field('version', u.unpack_one("B"), self.fmtstr)
            self.add_field('serial_number', u.unpack_one("B"), self.fmtstr)
            self.add_field('asset_tag', u.unpack_one("B"), self.fmtstr)
            minor_version_str = str(sm.header.minor_version) # 34 is .34, 4 is .4, 41 is .41; compare ASCIIbetically to compare initial digits rather than numeric value
            if self.length > 9:
                chassis_states = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'Safe',
                    0x04: 'Warning',
                    0x05: 'Critical',
                    0x06: 'Non-recoverable',
                }
                self.add_field('bootup_state', u.unpack_one("B"), unpack.format_table("{}", chassis_states))
                self.add_field('power_supply_state', u.unpack_one("B"), unpack.format_table("{}", chassis_states))
                self.add_field('thermal_state', u.unpack_one("B"), unpack.format_table("{}", chassis_states))
                security_states = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'None',
                    0x04: 'External interface locked out',
                    0x05: 'External interface enabled',
                }
                self.add_field('security_status', u.unpack_one("B"), unpack.format_table("{}", security_states))
            if self.length > 0xd:
                self.add_field('oem_defined', u.unpack_one("<I"))
            if self.length > 0x11:
                self.add_field('height', u.unpack_one("B"))
                self.add_field('num_power_cords', u.unpack_one("B"))
                self.add_field('contained_element_count', u.unpack_one("B"))
                self.add_field('contained_element_length', u.unpack_one("B"))
            if getattr(self, 'contained_element_count', 0):
                self.add_field('contained_elements', tuple(SystemEnclosureContainedElement(u, self.contained_element_length) for i in range(self.contained_element_count)))
            if self.length > (0x15 + (getattr(self, 'contained_element_count', 0) * getattr(self, 'contained_element_length', 0))):
                self.add_field('sku_number', u.unpack_one("B"), self.fmtstr)
        except:
            self.decode_failure = True
            print "Error parsing SystemEnclosure"
            import traceback
            traceback.print_exc()
        self.fini()

class SystemEnclosureContainedElement(unpack.Struct):
    def __init__(self, u, length):
        super(SystemEnclosureContainedElement, self).__init__()
        self.start_offset = u.offset
        self.raw_data = u.unpack_raw(length)
        self.u = unpack.Unpackable(self.raw_data)
        u = self.u
        self.add_field('contained_element_type', u.unpack_one("B"))
        type_selections = {
            0: 'SMBIOS baseboard type enumeration',
            1: 'SMBIOS structure type enumeration',
        }
        self.add_field('type_select', bitfields.getbits(self.contained_element_type, 7), unpack.format_table("contained_element_type[7]={}", type_selections))
        self.add_field('type', bitfields.getbits(self.contained_element_type, 6, 0))
        if self.type_select == 0:
            self.add_field('smbios_board_type', self.type, unpack.format_table("{}", _board_types))
        else:
            self.add_field('smbios_structure_type', self.type)
        self.add_field('minimum', u.unpack_one("B"))
        self.add_field('maximum', u.unpack_one("B"))
        if not u.at_end():
            self.add_field('data', u.unpack_rest())
        del self.u

class ProcessorInformation(SmbiosBaseStructure):
    smbios_structure_type = 4

    def __init__(self, u, sm):
        super(ProcessorInformation, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('socket_designation', u.unpack_one("B"), self.fmtstr)
            processor_types = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: 'Central Processor',
                0x04: 'Math Processor',
                0x05: 'DSP Processor',
                0x06: 'Video Processor',
            }
            self.add_field('processor_type', u.unpack_one("B"), unpack.format_table("{}", processor_types))
            self.add_field('processor_family', u.unpack_one("B"))
            self.add_field('processor_manufacturer', u.unpack_one("B"), self.fmtstr)
            self.add_field('processor_id', u.unpack_one("<Q"))
            self.add_field('processor_version', u.unpack_one("B"), self.fmtstr)
            self.add_field('voltage', u.unpack_one("B"))
            self.add_field('external_clock', u.unpack_one("<H"))
            self.add_field('max_speed', u.unpack_one("<H"))
            self.add_field('current_speed', u.unpack_one("<H"))
            self.add_field('status', u.unpack_one("B"))
            processor_upgrades = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: 'Daughter Board',
                0x04: 'ZIF Socket',
                0x05: 'Replaceable Piggy Back',
                0x06: 'None',
                0x07: 'LIF Socket',
                0x08: 'Slot 1',
                0x09: 'Slot 2',
                0x0A: '370-pin socket',
                0x0B: 'Slot A',
                0x0C: 'Slot M',
                0x0D: 'Socket 423',
                0x0E: 'Socket A (Socket 462)',
                0x0F: 'Socket 478',
                0x10: 'Socket 754',
                0x11: 'Socket 940',
                0x12: 'Socket 939',
                0x13: 'Socket mPGA604',
                0x14: 'Socket LGA771',
                0x15: 'Socket LGA775',
                0x16: 'Socket S1',
                0x17: 'Socket AM2',
                0x18: 'Socket F (1207)',
                0x19: 'Socket LGA1366',
                0x1A: 'Socket G34',
                0x1B: 'Socket AM3',
                0x1C: 'Socket C32',
                0x1D: 'Socket LGA1156',
                0x1E: 'Socket LGA1567',
                0x1F: 'Socket PGA988A',
                0x20: 'Socket BGA1288',
                0x21: 'Socket rPGA988B',
                0x22: 'Socket BGA1023',
                0x23: 'Socket BGA1224',
                0x24: 'Socket BGA1155',
                0x25: 'Socket LGA1356',
                0x26: 'Socket LGA2011',
                0x27: 'Socket FS1',
                0x28: 'Socket FS2',
                0x29: 'Socket FM1',
                0x2A: 'Socket FM2',
            }
            self.add_field('processor_upgrade', u.unpack_one("B"), unpack.format_table("{}", processor_upgrades))
            if self.length > 0x1A:
                self.add_field('l1_cache_handle', u.unpack_one("<H"))
                self.add_field('l2_cache_handle', u.unpack_one("<H"))
                self.add_field('l3_cache_handle', u.unpack_one("<H"))
            if self.length > 0x20:
                self.add_field('serial_number', u.unpack_one("B"), self.fmtstr)
                self.add_field('asset_tag', u.unpack_one("B"), self.fmtstr)
                self.add_field('part_number', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x24:
                self.add_field('core_count', u.unpack_one("B"))
                self.add_field('core_enabled', u.unpack_one("B"))
                self.add_field('thread_count', u.unpack_one("B"))
                self.add_field('processor_characteristics', u.unpack_one("<H"))
            if self.length > 0x28:
                self.add_field('processor_family_2', u.unpack_one("<H"))
            if self.length > 0x2A:
                self.add_field('core_count2', u.unpack_one("<H"))
                self.add_field('core_enabled2', u.unpack_one("<H"))
                self.add_field('thread_count2', u.unpack_one("<H"))
        except:
            self.decode_failure = True
            print "Error parsing Processor Information"
            import traceback
            traceback.print_exc()
        self.fini()

class MemoryControllerInformation(SmbiosBaseStructure): #obsolete starting with v2.1
    smbios_structure_type = 5

    def __init__(self, u, sm):
        super(MemoryControllerInformation, self).__init__(u, sm)
        u = self.u
        try:
            _error_detecting_method = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: 'None',
                0x04: '8-bit Parity',
                0x05: '32-bit ECC',
                0x06: '64-bit ECC',
                0x07: '128-bit ECC',
                0x08: 'CRC'
                }
            self.add_field('error_detecting_method', u.unpack_one("B"), unpack.format_table("{}", _error_detecting_method))
            self.add_field('error_correcting_capability', u.unpack_one("B"))
            _interleaves = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: 'One-Way Interleave',
                0x04: 'Two-Way Interleave',
                0x05: 'Four-Way Interleave',
                0x06: 'Eight-Way Interleave',
                0x07: 'Sixteen-Way Interleave'
                }
            self.add_field('supported_interleave', u.unpack_one("B"), unpack.format_table("{}", _interleaves))
            self.add_field('current_interleave', u.unpack_one("B"), unpack.format_table("{}", _interleaves))
            self.add_field('max_memory_module_size', u.unpack_one("B"), self.fmtstr)
            self.add_field('supported_speeds', u.unpack_one("<H"))
            self.add_field('supported_memory_types', u.unpack_one("<H"))
            self.add_field('memory_module_voltage', u.unpack_one("B"))
            self.add_field('req_voltage_b2', bitfields.getbits(self.memory_module_voltage, 2), "memory_module_voltage[2]={}")
            self.add_field('req_voltage_b1', bitfields.getbits(self.memory_module_voltage, 1), "memory_module_voltage[1]={}")
            self.add_field('req_voltage_b0', bitfields.getbits(self.memory_module_voltage, 0), "memory_module_voltage[0]={}")
            self.add_field('num_associated_memory_slots', u.unpack_one("B"))
            self.add_field('memory_module_configuration_handles', u.unpack_one("<(self.num_associated_memory_slots)H"))
            self.add_field('enabled_error_correcting_capabilities', u.unpack_one("B"))
        except:
            self.decode_failure = True
            print "Error parsing MemoryControllerInformation"
            import traceback
            traceback.print_exc()
        self.fini()

class MemoryModuleInformation(SmbiosBaseStructure): #obsolete starting with v2.1
    smbios_structure_type = 6

    def __init__(self, u, sm):
        super(MemoryModuleInformation, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('socket_designation', u.unpack_one("B"), self.fmtstr)
            self.add_field('bank_connections', u.unpack_one("B"))
            self.add_field('current_speed', u.unpack_one("B"))
            self.add_field('current_memory_type', u.unpack_one("<H"))
            _mem_connection = {
                0: 'single',
                1: 'double-bank'
                }
            self.add_field('installed_mem', u.unpack_one("B"))
            self.add_field('installed_size', bitfields.getbits(self.installed_mem, 6, 0), "installed_mem[6:0]={}")
            self.add_field('installed_memory_module_connection', bitfields.getbits(self.installed_mem, 7), unpack.format_table("installed_mem[7]={}", _mem_connection))
            self.add_field('enabled_mem', u.unpack_one("B"))
            self.add_field('enabled_size', bitfields.getbits(self.installed_mem, 6, 0), "enabled_mem[6:0]={}")
            self.add_field('enabled_memory_module_connection', bitfields.getbits(self.installed_mem, 7), unpack.format_table("enabled_mem[7]={}", _mem_connection))
            self.add_field('error_status', u.unpack_one("B"))
            self.add_field('error_status_info_obstained_from_event_log', bool(bitfields.getbits(self.error_status, 2)), unpack.format_table("error_status[2]={}", _mem_connection))
            self.add_field('correctable_errors_received', bool(bitfields.getbits(self.error_status, 1)), unpack.format_table("error_status[1]={}", _mem_connection))
            self.add_field('uncorrectable_errors_received', bool(bitfields.getbits(self.error_status, 0)), unpack.format_table("error_status[0]={}", _mem_connection))
        except:
            self.decode_failure = True
            print "Error parsing MemoryModuleInformation"
            import traceback
            traceback.print_exc()
        self.fini()

class CacheInformation(SmbiosBaseStructure):
    smbios_structure_type = 7

    def __init__(self, u, sm):
        super(CacheInformation, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('socket_designation', u.unpack_one("B"), self.fmtstr)
            processor_types = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: 'Central Processor',
                0x04: 'Math Processor',
                0x05: 'DSP Processor',
                0x06: 'Video Processor',
            }
            self.add_field('cache_configuration', u.unpack_one("<H"))
            _operational_mode = {
                0b00: 'Write Through',
                0b01: 'Write Back',
                0b10: 'Varies with Memory Address',
                0b11: 'Unknown'
                }
            self.add_field('operational_mode', bitfields.getbits(self.cache_configuration, 9, 8), unpack.format_table("cache_configuration[9:8]={}", _operational_mode))
            self.add_field('enabled_at_boot_time', bool(bitfields.getbits(self.cache_configuration, 7)), "cache_configuration[7]={}")
            _location = {
                0b00: 'Internal',
                0b01: 'External',
                0b10: 'Reserved',
                0b11: 'Unknown'
                }
            self.add_field('location_relative_to_cpu_module', bitfields.getbits(self.cache_configuration, 6, 5), unpack.format_table("cache_configuration[6:5]={}", _location))
            self.add_field('cache_socketed', bool(bitfields.getbits(self.cache_configuration, 3)), "cache_configuration[3]={}")
            self.add_field('cache_level', bitfields.getbits(self.cache_configuration, 2, 0), "cache_configuration[2:0]={}")
            self.add_field('max_cache_size', u.unpack_one("<H"))
            _granularity = {
                0: '1K granularity',
                1: '64K granularity'
                }
            self.add_field('max_granularity', bitfields.getbits(self.cache_configuration, 15), unpack.format_table("max_cache_size[15]={}", _granularity))
            self.add_field('max_size_in_granularity', bitfields.getbits(self.cache_configuration, 14, 0), "max_cache_size[14, 0]={}")
            self.add_field('installed_size', u.unpack_one("<H"))
            if self.installed_size != 0:
                self.add_field('installed_granularity', bitfields.getbits(self.cache_configuration, 15), unpack.format_table("installed_size[15]={}", _granularity))
                self.add_field('installed_size_in_granularity', bitfields.getbits(self.cache_configuration, 14, 0), "installed_size[14, 0]={}")
            self.add_field('supported_sram_type', u.unpack_one("<H"))
            self.add_field('current_sram_type', u.unpack_one("<H"))
            if self.length > 0x0F:
                self.add_field('cache_speed', u.unpack_one("B"))
            if self.length > 0x10:
                _error_correction = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'None',
                    0x04: 'Parity',
                    0x05: 'Single-bit ECC',
                    0x06: 'Multi-bit ECC'
                    }
                self.add_field('error_correction', u.unpack_one("B"), unpack.format_table("{}", _error_correction))
            if self.length > 0x10:
                _system_cache_type = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'Instruction',
                    0x04: 'Data',
                    0x05: 'Unified'
                    }
                self.add_field('system_cache_type', u.unpack_one("B"), unpack.format_table("{}", _system_cache_type))
            if self.length > 0x12:
                _associativity = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'Direct Mapped',
                    0x04: '2-way Set-Associative',
                    0x05: '4-way Set-Associative',
                    0x06: 'Fully Associative',
                    0x07: '8-way Set-Associative',
                    0x08: '16-way Set-Associative',
                    0x09: '12-way Set-Associative',
                    0x0A: '24-way Set-Associative',
                    0x0B: '32-way Set-Associative',
                    0x0C: '48-way Set-Associative',
                    0x0D: '64-way Set-Associative',
                    0x0E: '20-way Set-Associative'
                    }
                self.add_field('associativity', u.unpack_one("B"), unpack.format_table("{}", _associativity))

        except:
            self.decode_failure = True
            print "Error parsing CacheInformation"
            import traceback
            traceback.print_exc()
        self.fini()

class PortConnectorInfo(SmbiosBaseStructure):
    smbios_structure_type = 8

    def __init__(self, u, sm):
        super(PortConnectorInfo, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('internal_reference_designator', u.unpack_one("B"), self.fmtstr)
            connector_types = {
                0x00: 'None',
                0x01: 'Centronics',
                0x02: 'Mini Centronics',
                0x03: 'Proprietary',
                0x04: 'DB-25 pin male',
                0x05: 'DB-25 pin female',
                0x06: 'DB-15 pin male',
                0x07: 'DB-15 pin female',
                0x08: 'DB-9 pin male',
                0x09: 'DB-9 pin female',
                0x0A: 'RJ-11',
                0x0B: 'RJ-45',
                0x0C: '50-pin MiniSCSI',
                0x0D: 'Mini-DIN',
                0x0E: 'Micro-DIN',
                0x0F: 'PS/2',
                0x10: 'Infrared',
                0x11: 'HP-HIL',
                0x12: 'Access Bus (USB)',
                0x13: 'SSA SCSI',
                0x14: 'Circular DIN-8 male',
                0x15: 'Circular DIN-8 female',
                0x16: 'On Board IDE',
                0x17: 'On Board Floppy',
                0x18: '9-pin Dual Inline (pin 10 cut)',
                0x19: '25-pin Dual Inline (pin 26 cut)',
                0x1A: '50-pin Dual Inline',
                0x1B: '68-pin Dual Inline',
                0x1C: 'On Board Sound Input from CD-ROM',
                0x1D: 'Mini-Centronics Type-14',
                0x1E: 'Mini-Centronics Type-26',
                0x1F: 'Mini-jack (headphones)',
                0x20: 'BNC',
                0x21: '1394',
                0x22: 'SAS/SATA Plug Receptacle',
                0xA0: 'PC-98',
                0xA1: 'PC-98Hireso',
                0xA2: 'PC-H98',
                0xA3: 'PC-98Note',
                0xA4: 'PC-98Full',
                0xFF: 'Other',
            }
            self.add_field('internal_connector_type', u.unpack_one("B"), unpack.format_table("{}", connector_types))
            self.add_field('external_reference_designator', u.unpack_one("B"), self.fmtstr)
            self.add_field('external_connector_type', u.unpack_one("B"), unpack.format_table("{}", connector_types))
            port_types = {
                0x00: 'None',
                0x01: 'Parallel Port XT/AT Compatible',
                0x02: 'Parallel Port PS/2',
                0x03: 'Parallel Port ECP',
                0x04: 'Parallel Port EPP',
                0x05: 'Parallel Port ECP/EPP',
                0x06: 'Serial Port XT/AT Compatible',
                0x07: 'Serial Port 16450 Compatible',
                0x08: 'Serial Port 16550 Compatible',
                0x09: 'Serial Port 16550A Compatible',
                0x0A: 'SCSI Port',
                0x0B: 'MIDI Port',
                0x0C: 'Joy Stick Port',
                0x0D: 'Keyboard Port',
                0x0E: 'Mouse Port',
                0x0F: 'SSA SCSI',
                0x10: 'USB',
                0x11: 'FireWire (IEEE P1394)',
                0x12: 'PCMCIA Type I2',
                0x13: 'PCMCIA Type II',
                0x14: 'PCMCIA Type III',
                0x15: 'Cardbus',
                0x16: 'Access Bus Port',
                0x17: 'SCSI II',
                0x18: 'SCSI Wide',
                0x19: 'PC-98',
                0x1A: 'PC-98-Hireso',
                0x1B: 'PC-H98',
                0x1C: 'Video Port',
                0x1D: 'Audio Port',
                0x1E: 'Modem Port',
                0x1F: 'Network Port',
                0x20: 'SATA',
                0x21: 'SAS',
                0xA0: '8251 Compatible',
                0xA1: '8251 FIFO Compatible',
                0xFF: 'Other',
            }
            self.add_field('port_type', u.unpack_one("B"), unpack.format_table("{}", port_types))
        except:
            self.decodeFailure = True
            print "Error parsing PortConnectorInfo"
            import traceback
            traceback.print_exc()
        self.fini()

class SystemSlots(SmbiosBaseStructure):
    smbios_structure_type = 9

    def __init__(self, u, sm):
        super(SystemSlots, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('designation', u.unpack_one("B"), self.fmtstr)
            _slot_types = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: 'ISA',
                0x04: 'MCA',
                0x05: 'EISA',
                0x06: 'PCI',
                0x07: 'PC Card (PCMCIA)',
                0x08: 'VL-VESA',
                0x09: 'Proprietary',
                0x0A: 'Processor Card Slot',
                0x0B: 'Proprietary Memory Card Slot',
                0x0C: 'I/O Riser Card Slot',
                0x0D: 'NuBus',
                0x0E: 'PCI 66MHz Capable',
                0x0F: 'AGP',
                0x10: 'AGP 2X',
                0x11: 'AGP 4X',
                0x12: 'PCI-X',
                0x13: 'AGP 8X',
                0xA0: 'PC-98/C20',
                0xA1: 'PC-98/C24',
                0xA2: 'PC-98/E',
                0xA3: 'PC-98/Local Bus',
                0xA4: 'PC-98/Card',
                0xA5: 'PCI Express',
                0xA6: 'PCI Express x1',
                0xA7: 'PCI Express x2',
                0xA8: 'PCI Express x4',
                0xA9: 'PCI Express x8',
                0xAA: 'PCI Express x16',
                0xAB: 'PCI Express Gen 2',
                0xAC: 'PCI Express Gen 2 x1',
                0xAD: 'PCI Express Gen 2 x2',
                0xAE: 'PCI Express Gen 2 x4',
                0xAF: 'PCI Express Gen 2 x8',
                0xB0: 'PCI Express Gen 2 x16',
                0xB1: 'PCI Express Gen 3',
                0xB2: 'PCI Express Gen 3 x1',
                0xB3: 'PCI Express Gen 3 x2',
                0xB4: 'PCI Express Gen 3 x4',
                0xB5: 'PCI Express Gen 3 x8',
                0xB6: 'PCI Express Gen 3 x16',
            }
            self.add_field('slot_type', u.unpack_one("B"), unpack.format_table("{}", _slot_types))
            _slot_data_bus_widths = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: '8 bit',
                0x04: '16 bit',
                0x05: '32 bit',
                0x06: '64 bit',
                0x07: '128 bit',
                0x08: '1x or x1',
                0x09: '2x or x2',
                0x0A: '4x or x4',
                0x0B: '8x or x8',
                0x0C: '12x or x12',
                0x0D: '16x or x16',
                0x0E: '32x or x32',
            }
            self.add_field('slot_data_bus_width', u.unpack_one('B'), unpack.format_table("{}", _slot_data_bus_widths))
            _current_usages = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: 'Available',
                0x04: 'In use',
            }
            self.add_field('current_usage', u.unpack_one('B'), unpack.format_table("{}", _current_usages))
            _slot_lengths = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: 'Short Length',
                0x04: 'Long Length',
            }
            self.add_field('slot_length', u.unpack_one('B'), unpack.format_table("{}", _slot_lengths))
            self.add_field('slot_id', u.unpack_one('<H'))
            self.add_field('characteristics1', u.unpack_one('B'))
            self.add_field('characteristics_unknown', bool(bitfields.getbits(self.characteristics1, 0)), "characteristics1[0]={}")
            self.add_field('provides_5_0_volts', bool(bitfields.getbits(self.characteristics1, 1)), "characteristics1[1]={}")
            self.add_field('provides_3_3_volts', bool(bitfields.getbits(self.characteristics1, 2)), "characteristics1[2]={}")
            self.add_field('shared_slot', bool(bitfields.getbits(self.characteristics1, 3)), "characteristics1[3]={}")
            self.add_field('supports_pc_card_16', bool(bitfields.getbits(self.characteristics1, 4)), "characteristics1[4]={}")
            self.add_field('supports_cardbus', bool(bitfields.getbits(self.characteristics1, 5)), "characteristics1[5]={}")
            self.add_field('supports_zoom_video', bool(bitfields.getbits(self.characteristics1, 6)), "characteristics1[6]={}")
            self.add_field('supports_modem_ring_resume', bool(bitfields.getbits(self.characteristics1, 7)), "characteristics1[7]={}")
            if self.length > 0x0C:
                self.add_field('characteristics2', u.unpack_one('B'))
                self.add_field('supports_PME', bool(bitfields.getbits(self.characteristics2, 0)), "characteristics2[0]={}")
                self.add_field('supports_hot_plug', bool(bitfields.getbits(self.characteristics2, 1)), "characteristics2[1]={}")
                self.add_field('supports_smbus', bool(bitfields.getbits(self.characteristics2, 2)), "characteristics2[2]={}")
            if self.length > 0x0D:
                self.add_field('segment_group_number', u.unpack_one('<H'))
                self.add_field('bus_number', u.unpack_one('B'))
                self.add_field('device_function_number', u.unpack_one('B'))
                self.add_field('device_number', bitfields.getbits(self.device_function_number, 7, 3), "device_function_number[7:3]={}")
                self.add_field('function_number', bitfields.getbits(self.device_function_number, 2, 0), "device_function_number[2:0]={}")
        except:
            self.decodeFailure = True
            print "Error parsing SystemSlots"
            import traceback
            traceback.print_exc()
        self.fini()

class OnBoardDevicesInformation(SmbiosBaseStructure):
    smbios_structure_type = 10

    def __init__(self, u, sm):
        super(OnBoardDevicesInformation, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('device_type', u.unpack_one("B"))
            self.add_field('device_enabled', bool(bitfields.getbits(self.device_type, 7)), "device_type[7]={}")
            _device_types = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: 'Video',
                0x04: 'SCSI Controller',
                0x05: 'Ethernet',
                0x06: 'Token Ring',
                0x07: 'Sound',
                0x08: 'PATA Controller',
                0x09: 'SATA Controller',
                0x0A: 'SAS Controller'
            }
            self.add_field('type_of_device', bitfields.getbits(self.device_type, 6, 0), unpack.format_table("device_type[6:0]={}", _device_types))
            self.add_field('description_string', u.unpack_one("B"), self.fmtstr)
        except:
            self.decodeFailure = True
            print "Error parsing OnBoardDevicesInformation"
            import traceback
            traceback.print_exc()
        self.fini()

class OEMStrings(SmbiosBaseStructure):
    smbios_structure_type = 11

    def __init__(self, u, sm):
        super(OEMStrings, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('count', u.unpack_one("B"))
        except:
            self.decodeFailure = True
            print "Error parsing OEMStrings"
            import traceback
            traceback.print_exc()
        self.fini()

class SystemConfigOptions(SmbiosBaseStructure):
    smbios_structure_type = 12

    def __init__(self, u, sm):
        super(SystemConfigOptions, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('count', u.unpack_one("B"))
        except:
            self.decodeFailure = True
            print "Error parsing SystemConfigOptions"
            import traceback
            traceback.print_exc()
        self.fini()

class BIOSLanguageInformation(SmbiosBaseStructure):
    smbios_structure_type = 13

    def __init__(self, u, sm):
        super(BIOSLanguageInformation, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('installable_languages', u.unpack_one("B"))
            if self.length > 0x05:
                self.add_field('flags', u.unpack_one('B'))
                self.add_field('abbreviated_format', bool(bitfields.getbits(self.flags, 0)), "flags[0]={}")
            if self.length > 0x6:
                u.skip(15)
                self.add_field('current_language', u.unpack_one('B'), self.fmtstr)
        except:
            self.decodeFailure = True
            print "Error parsing BIOSLanguageInformation"
            import traceback
            traceback.print_exc()
        self.fini()

class GroupAssociations(SmbiosBaseStructure):
    smbios_structure_type = 14

    def __init__(self, u, sm):
        super(GroupAssociations, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('group_name', u.unpack_one("B"), self.fmtstr)
            self.add_field('item_type', u.unpack_one('B'))
            self.add_field('item_handle', u.unpack_one('<H'))
        except:
            self.decodeFailure = True
            print "Error parsing GroupAssociations"
            import traceback
            traceback.print_exc()
        self.fini()

class SystemEventLog(SmbiosBaseStructure):
    smbios_structure_type = 15

    def __init__(self, u, sm):
        super(SystemEventLog, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('log_area_length', u.unpack_one("<H"))
            self.add_field('log_header_start_offset', u.unpack_one('<H'))
            self.add_field('log_data_start_offset', u.unpack_one('<H'))
            _access_method = {
                0x00: 'Indexed I/O: 1 8-bit index port, 1 8-bit data port',
                0x01: 'Indexed I/O: 2 8-bit index ports, 1 8-bit data port',
                0x02: 'Indexed I/O: 1 16-bit index port, 1 8-bit data port',
                0x03: 'Memory-mapped physical 32-bit address',
                0x04: 'Available through General-Purpose NonVolatile Data functions',
                xrange(0x05, 0x07F): 'Available for future assignment',
                xrange(0x80, 0xFF): 'BIOS Vendor/OEM-specific'
                }
            self.add_field('access_method', u.unpack_one('B'), unpack.format_table("{}", _access_method))
            self.add_field('log_status', u.unpack_one('B'))
            self.add_field('log_area_full', bool(bitfields.getbits(self.log_status, 1)), "log_status[1]={}")
            self.add_field('log_area_valid', bool(bitfields.getbits(self.log_status, 0)), "log_status[0]={}")
            self.add_field('log_change_token', u.unpack_one('<I'))
            self.add_field('access_method_address', u.unpack_one('<I'))
            if self.length > 0x14:
                _log_header_formats = {
                    0: 'No header',
                    1: 'Type 1 log header',
                    xrange(2, 0x7f): 'Available for future assignment',
                    xrange(0x80, 0xff): 'BIOS vendor or OEM-specific format'
                    }
                self.add_field('log_header_format', u.unpack_one("B"), unpack.format_table("{}", _log_header_formats))
            if self.length > 0x15:
                self.add_field('num_supported_log_type_descriptors', u.unpack_one('B'))
            if self.length > 0x16:
                self.add_field('length_log_type_descriptor', u.unpack_one('B'))
            if self.length != (0x17 + (self.num_supported_log_type_descriptors * self.length_log_type_descriptor)):
                print "Error: structure length ({}) != 0x17 + (num_supported_log_type_descriptors ({}) * length_log_type_descriptor({}))".format(self.length, self.num_supported_log_type_descriptors, self.length_log_type_descriptor)
                print "structure length = {}".format(self.length)
                print "num_supported_log_type_descriptors = {}".format(self.num_supported_log_type_descriptors)
                print "length_log_type_descriptor = {}".format(self.length_log_type_descriptor)
                self.decodeFailure = True
            self.add_field('descriptors', tuple(EventLogDescriptor.unpack(u) for i in range(self.num_supported_log_type_descriptors)), unpack.format_each("\n{!r}"))
        except:
            self.decodeFailure = True
            print "Error parsing SystemEventLog"
            import traceback
            traceback.print_exc()
        self.fini()

class EventLogDescriptor(unpack.Struct):
    @staticmethod
    def _unpack(u):
        _event_log_type_descriptors = {
            0x00: 'Reserved',
            0x01: 'Single-bit ECC memory error',
            0x02: 'Multi-bit ECC memory error',
            0x03: 'Parity memory error',
            0x04: 'Bus time-out',
            0x05: 'I/O Channel Check',
            0x06: 'Software NMI',
            0x07: 'POST Memory Resize',
            0x08: 'POST Error',
            0x09: 'PCI Parity Error',
            0x0A: 'PCI System Error',
            0x0B: 'CPU Failure',
            0x0C: 'EISA FailSafe Timer time-out',
            0x0D: 'Correctable memory log disabled',
            0x0E: 'Logging disabled for a specific Event Type - too many errors of the same type received in a short amount of time',
            0x0F: 'Reserved',
            0x10: 'System Limit Exceeded',
            0x11: 'Asynchronous hardware timer expired and issued a system reset',
            0x12: 'System configuration information',
            0x13: 'Hard-disk information',
            0x14: 'System reconfigured',
            0x15: 'Uncorrectable CPU-complex error',
            0x16: 'Log Area Reset/Cleared',
            0x17: 'System boot',
            xrange(0x18, 0x7F): 'Unused, available for assignment',
            xrange(0x80, 0xFE): 'Availalbe for system- and OEM-specific assignments',
            0xFF: 'End of log'
        }
        yield 'log_type', u.unpack_one('B'), unpack.format_table("{}", _event_log_type_descriptors)
        _event_log_format = {
            0x00: 'None',
            0x01: 'Handle',
            0x02: 'Multiple-Event',
            0x03: 'Multiple-Event Handle',
            0x04: 'POST Results Bitmap',
            0x05: 'System Management Type',
            0x06: 'Multiple-Event System Management Type',
            xrange(0x80, 0xFF): 'OEM assigned'
        }
        yield 'variable_data_format_type', u.unpack_one('B'), unpack.format_table("{}", _event_log_format)

class PhysicalMemoryArray(SmbiosBaseStructure):
    smbios_structure_type = 16

    def __init__(self, u, sm):
        super(PhysicalMemoryArray, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                _location_field = {
                    0x01: "Other",
                    0x02: "Unknown",
                    0x03: "System board or motherboard",
                    0x04: "ISA add-on card",
                    0x05: "EISA add-on card",
                    0x06: "PCI add-on card",
                    0x07: "MCA add-on card",
                    0x08: "PCMCIA add-on card",
                    0x09: "Proprietary add-on card",
                    0x0A: "NuBus",
                    0xA0: "PC-98/C20 add-on card",
                    0xA1: "PC-98/C24 add-on card",
                    0xA2: "PC-98/E add-on card",
                    0xA3: "PC-98/Local bus add-on card"
                    }
                self.add_field('location', u.unpack_one("B"), unpack.format_table("{}", _location_field))
            if self.length > 0x05:
                _use = {
                    0x01: "Other",
                    0x02: "Unknown",
                    0x03: "System memory",
                    0x04: "Video memory",
                    0x05: "Flash memory",
                    0x06: "Non-volatile RAM",
                    0x07: "Cache memory"
                    }
                self.add_field('use', u.unpack_one('B'), unpack.format_table("{}", _use))
            if self.length > 0x06:
                _error_correction = {
                    0x01: "Other",
                    0x02: "Unknown",
                    0x03: "None",
                    0x04: "Parity",
                    0x05: "Single-bit ECC",
                    0x06: "Multi-bit ECC",
                    0x07: "CRC"
                    }
                self.add_field('memory_error_correction', u.unpack_one('B'), unpack.format_table("{}", _error_correction))
            if self.length > 0x07:
                self.add_field('maximum_capacity', u.unpack_one('<I'))
            if self.length > 0x0B:
                self.add_field('memory_error_information_handle', u.unpack_one('<H'))
            if self.length > 0x0D:
                self.add_field('num_memory_devices', u.unpack_one('<H'))
            if self.length > 0x0F:
                self.add_field('extended_maximum_capacity', u.unpack_one('<Q'))
        except:
            self.decodeFailure = True
            print "Error parsing PhysicalMemoryArray"
            import traceback
            traceback.print_exc()
        self.fini()

class MemoryDevice(SmbiosBaseStructure):
    smbios_structure_type = 17

    def __init__(self, u, sm):
        super(MemoryDevice, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('physical_memory_array_handle', u.unpack_one("<H"))
            if self.length > 0x6:
                self.add_field('memory_error_information_handle', u.unpack_one("<H"))
            if self.length > 0x8:
                self.add_field('total_width', u.unpack_one("<H"))
            if self.length > 0xA:
                self.add_field('data_width', u.unpack_one("<H"))
            if self.length > 0xC:
                self.add_field('size', u.unpack_one("<H"))
            if self.length > 0xE:
                _form_factors = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'SIMM',
                    0x04: 'SIP',
                    0x05: 'Chip',
                    0x06: 'DIP',
                    0x07: 'ZIP',
                    0x08: 'Proprietary Card',
                    0x09: 'DIMM',
                    0x0A: 'TSOP',
                    0x0B: 'Row of chips',
                    0x0C: 'RIMM',
                    0x0D: 'SODIMM',
                    0x0E: 'SRIMM',
                    0x0F: 'FB-DIMM'
                    }
                self.add_field('form_factor', u.unpack_one("B"), unpack.format_table("{}", _form_factors))
            if self.length > 0xF:
                self.add_field('device_set', u.unpack_one("B"))
            if self.length > 0x10:
                self.add_field('device_locator', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x11:
                self.add_field('bank_locator', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x12:
                _memory_types = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'DRAM',
                    0x04: 'EDRAM',
                    0x05: 'VRAM',
                    0x06: 'SRAM',
                    0x07: 'RAM',
                    0x08: 'ROM',
                    0x09: 'FLASH',
                    0x0A: 'EEPROM',
                    0x0B: 'FEPROM',
                    0x0C: 'EPROM',
                    0x0D: 'CDRAM',
                    0x0E: '3DRAM',
                    0x0F: 'SDRAM',
                    0x10: 'SGRAM',
                    0x11: 'RDRAM',
                    0x12: 'DDR',
                    0x13: 'DDR2',
                    0x14: 'DDR2 FB-DIMM',
                    xrange(0x15, 0x17): 'Reserved',
                    0x18: 'DDR3',
                    0x19: 'FBD2'
                    }
                self.add_field('memory_type', u.unpack_one("B"), unpack.format_table("{}", _memory_types))
            if self.length > 0x13:
                self.add_field('type_detail', u.unpack_one('<H'))
            if self.length > 0x15:
                self.add_field('speed', u.unpack_one("<H"))
            if self.length > 0x17:
                self.add_field('manufacturer', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x18:
                self.add_field('serial_number', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x19:
                self.add_field('asset_tag', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x1A:
                self.add_field('part_number', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x1B:
                self.add_field('attributes', u.unpack_one("B"))
                self.add_field('rank', bitfields.getbits(self.attributes, 3, 0), "attributes[3:0]={}")
            if self.length > 0x1C:
                if self.size == 0x7FFF:
                    self.add_field('extended_size', u.unpack_one('<I'))
                    self.add_field('mem_size', bitfields.getbits(self.type_detail, 30, 0), "type_detail[30:0]={}")
                else:
                    u.skip(4)
            if self.length > 0x20:
                self.add_field('configured_memory_clock_speed', u.unpack_one("<H"))
            if self.length > 0x22:
                self.add_field('minimum_voltage', u.unpack_one("<H"))
            if self.length > 0x24:
                self.add_field('maximum_voltage', u.unpack_one("<H"))
            if self.length > 0x26:
                self.add_field('configured_voltage', u.unpack_one("<H"))
        except:
            self.decodeFailure = True
            print "Error parsing MemoryDevice"
            import traceback
            traceback.print_exc()
        self.fini()

class MemoryErrorInfo32Bit(SmbiosBaseStructure):
    smbios_structure_type = 18

    def __init__(self, u, sm):
        super(MemoryErrorInfo32Bit, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                _error_types = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'OK',
                    0x04: 'Bad read',
                    0x05: 'Parity error',
                    0x06: 'Single-bit error',
                    0x07: 'Double-bit error',
                    0x08: 'Multi-bit error',
                    0x09: 'Nibble error',
                    0x0A: 'Checksum error',
                    0x0B: 'CRC error',
                    0x0C: 'Corrected single-bit error',
                    0x0D: 'Corrected error',
                    0x0E: 'Uncorrectable error'
                    }
                self.add_field('error_type', u.unpack_one("B"), unpack.format_table("{}", _error_types))
            if self.length > 0x5:
                 _error_granularity_field = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'Device level',
                    0x04: 'Memory partition level'
                    }
                 self.add_field('error_granularity', u.unpack_one("B"), unpack.format_table("{}", _error_granularity_field))
            if self.length > 0x6:
                _error_operation_field = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'Read',
                    0x04: 'Write',
                    0x05: 'Partial write'
                    }
                self.add_field('error_operation', u.unpack_one("B"), unpack.format_table("{}", _error_operation_field))
            if self.length > 0x7:
                self.add_field('vendor_syndrome', u.unpack_one("<I"))
            if self.length > 0xB:
                self.add_field('memory_array_error_address', u.unpack_one("<I"))
            if self.length > 0xF:
                self.add_field('device_error_address', u.unpack_one("<I"))
            if self.length > 0x13:
                self.add_field('error_resolution', u.unpack_one("<I"))
        except:
            self.decodeFailure = True
            print "Error parsing MemoryErrorInfo32Bit"
            import traceback
            traceback.print_exc()
        self.fini()

class MemoryArrayMappedAddress(SmbiosBaseStructure):
    smbios_structure_type = 19

    def __init__(self, u, sm):
        super(MemoryArrayMappedAddress, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('starting_address', u.unpack_one("<I"))
                # if FFFF FFFF: address stored in Extended Starting Address
            if self.length > 0x8:
                self.add_field('ending_address', u.unpack_one("<I"))
            if self.length > 0xC:
                self.add_field('memory_array_handle', u.unpack_one("<H"))
            if self.length > 0xE:
                self.add_field('partition_width', u.unpack_one("B"))
            if self.length > 0xF:
                # valid if starting_address = FFFF FFFF
                if self.starting_address == 0xFFFFFFFF:
                    self.add_field('extended_starting_address', u.unpack_one("<Q"))
                    if self.length > 0x17:
                        self.add_field('extended_ending_address', u.unpack_one("<Q"))
                else:
                    u.skip(16)

        except:
            self.decodeFailure = True
            print "Error parsing MemoryArrayMappedAddress"
            import traceback
            traceback.print_exc()
        self.fini()

class MemoryDeviceMappedAddress(SmbiosBaseStructure):
    smbios_structure_type = 20

    def __init__(self, u, sm):
        super(MemoryDeviceMappedAddress, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('starting_address', u.unpack_one("<I"))
                # if FFFF FFFF: address stored in Extended Starting Address
            if self.length > 0x8:
                self.add_field('ending_address', u.unpack_one("<I"))
            if self.length > 0xC:
                self.add_field('memory_device_handle', u.unpack_one("<H"))
            if self.length > 0xE:
                self.add_field('memory_array_mapped_address_handle', u.unpack_one("<H"))
            if self.length > 0x10:
                self.add_field('partition_row_position', u.unpack_one("B"))
            if self.length > 0x11:
                self.add_field('interleave_position', u.unpack_one("B"))
            if self.length > 0x12:
                self.add_field('interleave_data_depth', u.unpack_one("B"))
            if self.length > 0x13:
                # valid if starting_address = FFFF FFFF
                if self.starting_address == 0xFFFFFFFF:
                    self.add_field('extended_starting_address', u.unpack_one("<Q"))
                    if self.length > 0x1B:
                        self.add_field('extended_ending_address', u.unpack_one("<Q"))
                else:
                    u.skip(16)
        except:
            self.decodeFailure = True
            print "Error parsing MemoryDeviceMappedAddress"
            import traceback
            traceback.print_exc()
        self.fini()

class BuiltInPointingDevice(SmbiosBaseStructure):
    smbios_structure_type = 21

    def __init__(self, u, sm):
        super(BuiltInPointingDevice, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                _pointing_device_types = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'Mouse',
                    0x04: 'Track Ball',
                    0x05: 'Track Point',
                    0x06: 'Glide Point',
                    0x07: 'Touch Pad',
                    0x08: 'Touch Screen',
                    0x09: 'Optical Sensor'
                    }
                self.add_field('pointing_device_type', u.unpack_one("B"), unpack.format_table("{}", _pointing_device_types))
            if self.length > 0x5:
                _interfaces = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'Serial',
                    0x04: 'PS/2',
                    0x05: 'Infared',
                    0x06: 'HP-HIL',
                    0x07: 'Bus mouse',
                    0x08: 'ADB (Apple Desktop Bus)',
                    0x09: 'Bus mouse DB-9',
                    0x0A: 'Bus mouse micro-DIN',
                    0x0B: 'USB'
                    }
                self.add_field('interface', u.unpack_one("B"), unpack.format_table("{}", _interfaces))
            if self.length > 0x6:
                self.add_field('num_buttons', u.unpack_one("B"))
        except:
            self.decodeFailure = True
            print "Error parsing BuiltInPointingDevice"
            import traceback
            traceback.print_exc()
        self.fini()

class PortableBattery(SmbiosBaseStructure):
    smbios_structure_type = 22

    def __init__(self, u, sm):
        super(PortableBattery, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('location', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x5:
                self.add_field('manufacturer', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x6:
                self.add_field('manufacturer_date', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x7:
                self.add_field('serial_number', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x8:
                self.add_field('device_name', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x9:
                _device_chemistry = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'Lead Acid',
                    0x04: 'Nickel Cadmium',
                    0x05: 'Nickel metal hydride',
                    0x06: 'Lithium-ion',
                    0x07: 'Zinc air',
                    0x08: 'Lithium Polymer'
                    }
                self.add_field('device_chemistry', u.unpack_one("B"), unpack.format_table("{}", _device_chemistry))
            if self.length > 0xA:
                self.add_field('design_capacity', u.unpack_one("<H"))
            if self.length > 0xC:
                self.add_field('design_voltage', u.unpack_one("<H"))
            if self.length > 0xE:
                self.add_field('sbds_version_number', u.unpack_one("B"), self.fmtstr)
            if self.length > 0xF:
                self.add_field('max_error_battery_data', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x10:
                if self.serial_number == 0:
                    self.add_field('sbds_serial_number', u.unpack_one("<H"))
                else:
                    u.skip(2)
            if self.length > 0x12:
                if self.manufacturer_date == 0:
                    self.add_field('sbds_manufacture_date', u.unpack_one("<H"))
                    self.add_field('year_biased_by_1980', bitfields.getbits(self.sbds_manufacture_date, 15, 9), "sbds_manufacture_date[15:9]={}")
                    self.add_field('month', bitfields.getbits(self.sbds_manufacture_date, 8, 5), "sbds_manufacture_date[8:5]={}")
                    self.add_field('date', bitfields.getbits(self.sbds_manufacture_date, 4, 0), "sbds_manufacture_date[4:0]={}")
                else:
                    u.skip(2)
            if self.length > 0x14:
                if self.device_chemistry == 0x02:
                    self.add_field('sbds_device_chemistry', u.unpack_one("B"), self.fmtstr)
                else:
                    u.skip(1)
            if self.length > 0x15:
                self.add_field('design_capacity_multiplier', u.unpack_one("B"))
            if self.length > 0x16:
                self.add_field('oem_specific', u.unpack_one("<I"))
        except:
            self.decodeFailure = True
            print "Error parsing PortableBattery"
            import traceback
            traceback.print_exc()
        self.fini()

class SystemReset(SmbiosBaseStructure):
    smbios_structure_type = 23

    def __init__(self, u, sm):
        super(SystemReset, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('capabilities', u.unpack_one("B"))
                self.add_field('contains_watchdog_timer', bool(bitfields.getbits(self.capabilities, 5)), "capabilities[5]={}")
                _boot_option = {
                    0b00: 'Reserved, do not use',
                    0b01: 'Operating System',
                    0b10: 'System utilities',
                    0b11: 'Do not reboot'
                    }
                self.add_field('boot_option_on_limit', bitfields.getbits(self.capabilities, 4, 3), unpack.format_table("capabilities[4:3]={}", _boot_option))
                self.add_field('boot_option_after_watchdog_reset', bitfields.getbits(self.capabilities, 2, 1), unpack.format_table("capabilities[2:1]={}", _boot_option))
                self.add_field('system_reset_enabled_by_user', bool(bitfields.getbits(self.capabilities, 0)), "capabilities[0]={}")
            if self.length > 0x5:
                self.add_field('reset_count', u.unpack_one("<H"))
            if self.length > 0x5:
                self.add_field('reset_limit', u.unpack_one("<H"))
            if self.length > 0x9:
                self.add_field('timer_interval', u.unpack_one("<H"))
            if self.length > 0xB:
                self.add_field('timeout', u.unpack_one("<H"))
        except:
            self.decodeFailure = True
            print "Error parsing SystemReset"
            import traceback
            traceback.print_exc()
        self.fini()

class HardwareSecurity(SmbiosBaseStructure):
    smbios_structure_type = 24

    def __init__(self, u, sm):
        super(HardwareSecurity, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('hardware_security_settings', u.unpack_one("B"))
                _status = {
                    0x00: 'Disabled',
                    0x01: 'Enabled',
                    0x02: 'Not Implemented',
                    0x03: 'Unknown'
                    }
                self.add_field('power_on_password_status', bitfields.getbits(self.hardware_security_settings, 7, 6), unpack.format_table("hardware_security_settings[7:6]={}", _status))
                self.add_field('keyboard_password_status', bitfields.getbits(self.hardware_security_settings, 5, 4), unpack.format_table("hardware_security_settings[5:4]={}", _status))
                self.add_field('admin_password_status', bitfields.getbits(self.hardware_security_settings, 3, 2), unpack.format_table("hardware_security_settings0[3:2]={}", _status))
                self.add_field('front_panel_reset_status', bitfields.getbits(self.hardware_security_settings, 1, 0), unpack.format_table("hardware_security_settings[1:0]={}", _status))
        except:
            self.decodeFailure = True
            print "Error parsing HardwareSecurity"
            import traceback
            traceback.print_exc()
        self.fini()

class SystemPowerControls(SmbiosBaseStructure):
    smbios_structure_type = 25

    def __init__(self, u, sm):
        super(SystemPowerControls, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('next_scheduled_poweron_month', u.unpack_one("B"))
                self.add_field('next_scheduled_poweron_day_of_month', u.unpack_one("B"))
                self.add_field('next_scheduled_poweron_hour', u.unpack_one("B"))
                self.add_field('next_scheduled_poweron_minute', u.unpack_one("B"))
                self.add_field('next_scheduled_poweron_second', u.unpack_one("B"))
        except:
            self.decodeFailure = True
            print "Error parsing SystemPowerControls"
            import traceback
            traceback.print_exc()
        self.fini()

class VoltageProbe(SmbiosBaseStructure):
    smbios_structure_type = 26

    def __init__(self, u, sm):
        super(VoltageProbe, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('description', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x5:
                self.add_field('location_and_status', u.unpack_one("B"))
                _status = {
                    0b001: 'Other',
                    0b010: 'Unknown',
                    0b011: 'OK',
                    0b100: 'Non-critical',
                    0b101: 'Critical',
                    0b110: 'Non-recoverable'
                    }
                _location = {
                    0b00001: 'Other',
                    0b00010: 'Unknown',
                    0b00011: 'Processor',
                    0b00100: 'Disk',
                    0b00101: 'Peripheral Bay',
                    0b00110: 'System Management Module',
                    0b00111: 'Motherboard',
                    0b01000: 'Memory Module',
                    0b01001: 'Processor Module',
                    0b01010: 'Power Unit',
                    0b01011: 'Add-in Card'
                    }
                self.add_field('status', bitfields.getbits(self.location_and_status, 7, 5), unpack.format_table("location_and_status[7:5]={}", _status))
                self.add_field('location', bitfields.getbits(self.location_and_status, 4, 0), unpack.format_table("location_and_status[4:0]={}", _location))
            if self.length > 0x6:
                self.add_field('max_value', u.unpack_one("<H"))
            if self.length > 0x8:
                self.add_field('min_value', u.unpack_one("<H"))
            if self.length > 0xA:
                self.add_field('resolution', u.unpack_one("<H"))
            if self.length > 0xC:
                self.add_field('tolerance', u.unpack_one("<H"))
            if self.length > 0xE:
                self.add_field('accuracy', u.unpack_one("<H"))
            if self.length > 0x10:
                self.add_field('oem_defined', u.unpack_one("<I"))
            if self.length > 0x14:
                self.add_field('nominal_value', u.unpack_one("<H"))
        except:
            self.decodeFailure = True
            print "Error parsing VoltageProbe"
            import traceback
            traceback.print_exc()
        self.fini()

class CoolingDevice(SmbiosBaseStructure):
    smbios_structure_type = 27

    def __init__(self, u, sm):
        super(CoolingDevice, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('temperature_probe_handle', u.unpack_one("<H"))
            if self.length > 0x6:
                self.add_field('device_type_and_status', u.unpack_one("B"))
                _status = {
                    0b001: 'Other',
                    0b010: 'Unknown',
                    0b011: 'OK',
                    0b100: 'Non-critical',
                    0b101: 'Critical',
                    0b110: 'Non-recoverable'
                    }
                _type = {
                    0b00001: 'Other',
                    0b00010: 'Unknown',
                    0b00011: 'Fan',
                    0b00100: 'Centrifugal Blower',
                    0b00101: 'Chip Fan',
                    0b00110: 'Cabinet Fan',
                    0b00111: 'Power Supply Fan',
                    0b01000: 'Heat Pipe',
                    0b01001: 'Integrated Refrigeration',
                    0b10000: 'Active Cooling',
                    0b10001: 'Passive Cooling'
                    }
                self.add_field('status', bitfields.getbits(self.device_type_and_status, 7, 5), unpack.format_table("device_type_and_status[7:5]={}", _status))
                self.add_field('device_type', bitfields.getbits(self.device_type_and_status, 4, 0), unpack.format_table("device_type_and_status[4:0]={}", _type))
            if self.length > 0x7:
                self.add_field('cooling_unit_group', u.unpack_one("B"))
            if self.length > 0x8:
                self.add_field('OEM_defined', u.unpack_one("<I"))
            if self.length > 0xC:
                self.add_field('nominal_speed', u.unpack_one("<H"))
            if self.length > 0xE:
               self.add_field('description', u.unpack_one("B"), self.fmtstr)
        except:
            self.decodeFailure = True
            print "Error parsing CoolingDevice"
            import traceback
            traceback.print_exc()
        self.fini()

class TemperatureProbe(SmbiosBaseStructure):
    smbios_structure_type = 28

    def __init__(self, u, sm):
        super(TemperatureProbe, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('description', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x5:
                self.add_field('location_and_status', u.unpack_one("B"))
                _status = {
                    0b001: 'Other',
                    0b010: 'Unknown',
                    0b011: 'OK',
                    0b100: 'Non-critical',
                    0b101: 'Critical',
                    0b110: 'Non-recoverable'
                    }
                _location = {
                    0b00001: 'Other',
                    0b00010: 'Unknown',
                    0b00011: 'Processor',
                    0b00100: 'Disk',
                    0b00101: 'Peripheral Bay',
                    0b00110: 'System Management Module',
                    0b00111: 'Motherboard',
                    0b01000: 'Memory Module',
                    0b01001: 'Processor Module',
                    0b01010: 'Power Unit',
                    0b01011: 'Add-in Card',
                    0b01100: 'Front Panel Board',
                    0b01101: 'Back Panel Board',
                    0b01110: 'Power System Board',
                    0b01111: 'Drive Back Plane'
                    }
                self.add_field('status', bitfields.getbits(self.location_and_status, 7, 5), unpack.format_table("location_and_status[7:5]={}", _status))
                self.add_field('location', bitfields.getbits(self.location_and_status, 4, 0), unpack.format_table("location_and_status[4:0]={}", _location))
            if self.length > 0x6:
                self.add_field('maximum_value', u.unpack_one("<H"))
            if self.length > 0x8:
                self.add_field('minimum_value', u.unpack_one("<H"))
            if self.length > 0xA:
                self.add_field('resolution', u.unpack_one("<H"))
            if self.length > 0xC:
                self.add_field('tolerance', u.unpack_one("<H"))
            if self.length > 0xE:
                self.add_field('accuracy', u.unpack_one("<H"))
            if self.length > 0x10:
                self.add_field('OEM_defined', u.unpack_one("<I"))
            if self.length > 0x14:
                self.add_field('nominal_value', u.unpack_one("<H"))
        except:
            self.decodeFailure = True
            print "Error parsing TemperatureProbe"
            import traceback
            traceback.print_exc()
        self.fini()

class ElectricalCurrentProbe(SmbiosBaseStructure):
    smbios_structure_type = 29

    def __init__(self, u, sm):
        super(ElectricalCurrentProbe, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('description', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x5:
                self.add_field('location_and_status', u.unpack_one("B"))
                _status = {
                    0b001: 'Other',
                    0b010: 'Unknown',
                    0b011: 'OK',
                    0b100: 'Non-critical',
                    0b101: 'Critical',
                    0b110: 'Non-recoverable'
                    }
                _location = {
                    0b00001: 'Other',
                    0b00010: 'Unknown',
                    0b00011: 'Processor',
                    0b00100: 'Disk',
                    0b00101: 'Peripheral Bay',
                    0b00110: 'System Management Module',
                    0b00111: 'Motherboard',
                    0b01000: 'Memory Module',
                    0b01001: 'Processor Module',
                    0b01010: 'Power Unit',
                    0b01011: 'Add-in Card',
                    0b01100: 'Front Panel Board',
                    0b01101: 'Back Panel Board',
                    0b01110: 'Power System Board',
                    0b01111: 'Drive Back Plane'
                    }
                self.add_field('status', bitfields.getbits(self.location_and_status, 7, 5), unpack.format_table("location_and_status[7:5]={}", _status))
                self.add_field('location', bitfields.getbits(self.location_and_status, 4, 0), unpack.format_table("location_and_status[4:0]={}", _location))
            if self.length > 0x6:
                self.add_field('maximum_value', u.unpack_one("<H"))
            if self.length > 0x8:
                self.add_field('minimum_value', u.unpack_one("<H"))
            if self.length > 0xA:
                self.add_field('resolution', u.unpack_one("<H"))
            if self.length > 0xC:
                self.add_field('tolerance', u.unpack_one("<H"))
            if self.length > 0xE:
                self.add_field('accuracy', u.unpack_one("<H"))
            if self.length > 0x10:
                self.add_field('OEM_defined', u.unpack_one("<I"))
            if self.length > 0x14:
                self.add_field('nominal_value', u.unpack_one("<H"))
        except:
            self.decodeFailure = True
            print "Error parsing ElectricalCurrentProbe"
            import traceback
            traceback.print_exc()
        self.fini()

class OutOfBandRemoteAccess(SmbiosBaseStructure):
    smbios_structure_type = 30

    def __init__(self, u, sm):
        super(OutOfBandRemoteAccess, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('manufacturer_name', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x5:
                self.add_field('connections', u.unpack_one("B"))
                self.add_field('outbound_connection_enabled', bool(bitfields.getbits(self.connections, 1)), "connections[1]={}")
                self.add_field('inbound_connection_enabled', bool(bitfields.getbits(self.connections, 0)), "connections[0]={}")
        except:
            self.decodeFailure = True
            print "Error parsing OutOfBandRemoteAccess"
            import traceback
            traceback.print_exc()
        self.fini()

class BootIntegrityServicesEntryPoint(SmbiosBaseStructure):
    smbios_structure_type = 31

class SystemBootInformation(SmbiosBaseStructure):
    smbios_structure_type = 32

    def __init__(self, u, sm):
        super(SystemBootInformation, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0xA:
                u.skip(6)
                _boot_status = {
                    0: 'No errors detected',
                    1: 'No bootable media',
                    2: '"normal" operating system failed to load',
                    3: 'Firmware-detected hardware failure, including "unknown" failure types',
                    4: 'Operating system-detected hardware failure',
                    5: 'User-requested boot, usually through a keystroke',
                    6: 'System security violation',
                    7: 'Previously-requested image',
                    8: 'System watchdog timer expired, causing the system to reboot',
                    xrange(9,127): 'Reserved for future assignment',
                    xrange(128, 191): 'Vendor/OEM-specific implementations',
                    xrange(192, 255): 'Product-specific implementations'
                    }
                self.add_field('boot_status', u.unpack_one("B"), unpack.format_table("{}", _boot_status))
        except:
            self.decodeFailure = True
            print "Error parsing SystemBootInformation"
            import traceback
            traceback.print_exc()
        self.fini()

class MemoryErrorInfo64Bit(SmbiosBaseStructure):
    smbios_structure_type = 33

    def __init__(self, u, sm):
        super(MemoryErrorInfo64Bit, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                _error_types = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'OK',
                    0x04: 'Bad read',
                    0x05: 'Parity error',
                    0x06: 'Single-bit error',
                    0x07: 'Double-bit error',
                    0x08: 'Multi-bit error',
                    0x09: 'Nibble error',
                    0x0A: 'Checksum error',
                    0x0B: 'CRC error',
                    0x0C: 'Corrected single-bit error',
                    0x0D: 'Corrected error',
                    0x0E: 'Uncorrectable error'
                    }
                self.add_field('error_type', u.unpack_one("B"), unpack.format_table("{}", _error_types))
            if self.length > 0x5:
                 _error_granularity_field = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'Device level',
                    0x04: 'Memory partition level'
                    }
                 self.add_field('error_granularity', u.unpack_one("B"), unpack.format_table("{}", _error_granularity_field))
            if self.length > 0x6:
                _error_operation_field = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'Read',
                    0x04: 'Write',
                    0x05: 'Partial write'
                    }
                self.add_field('error_operation', u.unpack_one("B"), unpack.format_table("{}", _error_operation_field))
            if self.length > 0x7:
                self.add_field('vendor_syndrome', u.unpack_one("<I"))
            if self.length > 0xB:
                self.add_field('memory_array_error_address', u.unpack_one("<Q"))
            if self.length > 0xF:
                self.add_field('device_error_address', u.unpack_one("<Q"))
            if self.length > 0x13:
                self.add_field('error_resolution', u.unpack_one("<Q"))
        except:
            self.decodeFailure = True
            print "Error parsing MemoryErrorInfo64Bit"
            import traceback
            traceback.print_exc()
        self.fini()

class ManagementDevice(SmbiosBaseStructure):
    smbios_structure_type = 34

    def __init__(self, u, sm):
        super(ManagementDevice, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('description', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x5:
                _type = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'National Semiconductor LM75',
                    0x04: 'National Semiconductor LM78',
                    0x05: 'National Semiconductor LM79',
                    0x06: 'National Semiconductor LM80',
                    0x07: 'National Semiconductor LM81',
                    0x08: 'Analog Devices ADM9240',
                    0x09: 'Dallas Semiconductor DS1780',
                    0x0A: 'Maxim 1617',
                    0x0B: 'Genesys GL518SM',
                    0x0C: 'Winbond W83781D',
                    0x0D: 'Holtek HT82H791'
                    }
                self.add_field('device_type', u.unpack_one("B"), unpack.format_table("{}", _type))
            if self.length > 0x6:
                self.add_field('address', u.unpack_one("<I"))
            if self.length > 0xA:
                 _address_type = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'I/O Port',
                    0x04: 'Memory',
                    0x05: 'SM Bus'
                    }
                 self.add_field('address_type', u.unpack_one("B"), unpack.format_table("{}", _address_type))
        except:
            self.decodeFailure = True
            print "Error parsing ManagementDevice"
            import traceback
            traceback.print_exc()
        self.fini()

class ManagementDeviceComponent(SmbiosBaseStructure):
    smbios_structure_type = 35

    def __init__(self, u, sm):
        super(ManagementDeviceComponent, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('description', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x5:
                self.add_field('management_device_handle', u.unpack_one("<H"))
            if self.length > 0x7:
                self.add_field('component_handle', u.unpack_one("<H"))
            if self.length > 0x9:
                self.add_field('threshold_handle', u.unpack_one("<H"))
        except:
            self.decodeFailure = True
            print "Error parsing ManagementDeviceComponent"
            import traceback
            traceback.print_exc()
        self.fini()

class ManagementDeviceThresholdData(SmbiosBaseStructure):
    smbios_structure_type = 36

    def __init__(self, u, sm):
        super(ManagementDeviceThresholdData, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('lower_threshold_noncritical', u.unpack_one("<H"))
            if self.length > 0x6:
                self.add_field('upper_threshold_noncritical', u.unpack_one("<H"))
            if self.length > 0x8:
                self.add_field('lower_threshold_critical', u.unpack_one("<H"))
            if self.length > 0xA:
                self.add_field('upper_threshold_critical', u.unpack_one("<H"))
            if self.length > 0xC:
                self.add_field('lower_threshold_nonrecoverable', u.unpack_one("<H"))
            if self.length > 0xE:
                self.add_field('upper_threshold_nonrecoverable', u.unpack_one("<H"))
        except:
            self.decodeFailure = True
            print "Error parsing ManagementDeviceThresholdData"
            import traceback
            traceback.print_exc()
        self.fini()

class MemoryChannel(SmbiosBaseStructure):
    smbios_structure_type = 37

    def __init__(self, u, sm):
        super(MemoryChannel, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                _channel_type = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'RamBus',
                    0x04: 'SyncLink'
                    }
                self.add_field('channel_type', u.unpack_one("B"), unpack.format_table("{}", _channel_type))
            if self.length > 0x6:
                self.add_field('max_channel_load', u.unpack_one("B"))
            if self.length > 0x8:
                self.add_field('memory_device_count', u.unpack_one("B"))
            if self.length > 0xA:
                self.add_field('memory_device_load', u.unpack_one("B"))
            if self.length > 0xC:
                self.add_field('memory_device_handle', u.unpack_one("<H"))
        except:
            self.decodeFailure = True
            print "Error parsing MemoryChannel"
            import traceback
            traceback.print_exc()
        self.fini()

class IPMIDeviceInformation(SmbiosBaseStructure):
    smbios_structure_type = 38

    def __init__(self, u, sm):
        super(IPMIDeviceInformation, self).__init__(u, sm)
        u = self.u
        try:
            _interface_type = {
                0x00: 'Unknown',
                0x01: 'KCS: Keyboard Controller Style',
                0x02: 'SMIC: Server Management Interface Chip',
                0x03: 'BT: Block Transfer',
                xrange(0x04, 0xFF): 'Reserved'
                }
            self.add_field('interface_type', u.unpack_one("B"), unpack.format_table("{}", _interface_type))
            self.add_field('ipmi_specification_revision', u.unpack_one("B"))
            self.add_field('msd_revision', bitfields.getbits(self.ipmi_specification_revision, 7, 4), "ipmi_specification_revision[7:4]={}")
            self.add_field('lsd_revision', bitfields.getbits(self.ipmi_specification_revision, 3, 0), "ipmi_specification_revision[3:0]={}")

            self.add_field('i2c_slave_address', u.unpack_one("B"))
            self.add_field('nv_storage_device_address', u.unpack_one("B"))
            self.add_field('base_address', u.unpack_one("<Q"))
            # if lsb is 1, address is in IO space. otherwise, memory-mapped
            self.add_field('base_address_modifier_interrupt_info', u.unpack_one("B"))
            _reg_spacing = {
                0b00: 'Interface registers are on successive byte boundaries',
                0b01: 'Interface registers are on 32-bit boundaries',
                0b10: 'Interface registers are on 16-byte boundaries',
                0b11: 'Reserved'
                }
            self.add_field('register_spacing', bitfields.getbits(self.base_address_modifier_interrupt_info, 7, 6), unpack.format_table("base_address_modifier_interrupt_info[7:6]={}", _reg_spacing))
            self.add_field('ls_bit_for_addresses', bitfields.getbits(self.base_address_modifier_interrupt_info, 4), "base_address_modifier_interrupt_info[4]={}")
            self.add_field('interrupt_info_specified', bool(bitfields.getbits(self.base_address_modifier_interrupt_info, 3)), "base_address_modifier_interrupt_info[3]={}")
            _polarity = {
                0: 'active low',
                1: 'active high'
                }
            self.add_field('interrupt_polarity', bitfields.getbits(self.base_address_modifier_interrupt_info, 1), unpack.format_table("base_address_modifier_interrupt_info[1]={}", _polarity))
            _interrupt_trigger = {
                0: 'edge',
                1: 'level'
                }
            self.add_field('interrupt_trigger_mode', bitfields.getbits(self.base_address_modifier_interrupt_info, 0), unpack.format_table("base_address_modifier_interrupt_info[0]={}", _interrupt_trigger))
            self.add_field('interrupt_number', u.unpack_one("B"))
        except:
            self.decodeFailure = True
            print "Error parsing IPMIDeviceInformation"
            import traceback
            traceback.print_exc()
        self.fini()

class SystemPowerSupply(SmbiosBaseStructure):
    smbios_structure_type = 39

    def __init__(self, u, sm):
        super(SystemPowerSupply, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('power_unit_group', u.unpack_one("B"))
            if self.length > 0x5:
                self.add_field('location', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x6:
                self.add_field('device_name', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x7:
                self.add_field('manufacturer', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x8:
                self.add_field('serial_number', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x9:
                self.add_field('asset_tag', u.unpack_one("B"), self.fmtstr)
            if self.length > 0xA:
                self.add_field('model_part_number', u.unpack_one("B"), self.fmtstr)
            if self.length > 0xB:
                self.add_field('revision_level', u.unpack_one("B"), self.fmtstr)
            if self.length > 0xC:
                self.add_field('max_power_capacity', u.unpack_one("<H"))
            if self.length > 0xE:
                self.add_field('power_supply_characteristics', u.unpack_one("<H"))
                _dmtf_power_supply_type = {
                    0b001: 'Other',
                    0b010: 'Unknown',
                    0b011: 'Linear',
                    0b100: 'Switching',
                    0b101: 'Battery',
                    0b110: 'UPS',
                    0b111: 'Converter',
                    0b1000: 'Regulator',
                    xrange(0b1001, 0b1111): 'Reserved'
                    }
                self.add_field('dmtf_power_supply_type', bitfields.getbits(self.power_supply_characteristics, 13, 10), unpack.format_table("power_supply_characteristics[13:10]={}", _dmtf_power_supply_type))
                _status = {
                    0b001: 'Other',
                    0b010: 'Unknown',
                    0b011: 'OK',
                    0b100: 'Non-critical',
                    0b101: 'Critical; power supply has failed and has been taken off-line'
                    }
                self.add_field('status', bitfields.getbits(self.power_supply_characteristics, 9, 7), unpack.format_table("power_supply_characteristics[9:7]={}", _status))
                _dmtf_input_voltage_range_switching = {
                    0b001: 'Other',
                    0b010: 'Unknown',
                    0b011: 'Manual',
                    0b100: 'Auto-switch',
                    0b101: 'Wide range',
                    0b110: 'Not applicable',
                    xrange(0b0111, 0b1111): 'Reserved'
                    }
                self.add_field('dmtf_input_voltage_range_switching', bitfields.getbits(self.power_supply_characteristics, 6, 3), unpack.format_table("power_supply_characteristics[6:3]={}", _dmtf_input_voltage_range_switching))
                self.add_field('power_supply_unplugged', bool(bitfields.getbits(self.power_supply_characteristics, 2)), "power_supply_characteristics[2]={}")
                self.add_field('power_supply_present', bool(bitfields.getbits(self.power_supply_characteristics, 1)), "power_supply_characteristics[1]={}")
                self.add_field('power_supply_hot_replaceable', bool(bitfields.getbits(self.power_supply_characteristics, 0)), "power_supply_characteristics[0]={}")
            if self.length > 0x10:
                self.add_field('input_voltage_probe_handle', u.unpack_one("<H"))
            if self.length > 0x12:
                self.add_field('cooling_device_handle', u.unpack_one("<H"))
            if self.length > 0x14:
                self.add_field('input_current_probe_handle', u.unpack_one("<H"))
        except:
            self.decodeFailure = True
            print "Error parsing SystemPowerSupply"
            import traceback
            traceback.print_exc()
        self.fini()

class AdditionalInformation(SmbiosBaseStructure):
    smbios_structure_type = 40

    def __init__(self, u, sm):
        super(AdditionalInformation, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('num_additional_information_entries', u.unpack_one("B"))
            if self.length > 0x5:
                self.add_field('additional_information_entry_length', u.unpack_one("B"))
                self.add_field('referenced_handle', u.unpack_one("<H"))
                self.add_field('referenced_offset', u.unpack_one("B"))
                self.add_field('string', u.unpack_one("B"), self.fmtstr)
                self.add_field('value', u.unpack_rest())
        except:
            self.decodeFailure = True
            print "Error parsing AdditionalInformation"
            import traceback
            traceback.print_exc()
        self.fini()

class OnboardDevicesExtendedInformation(SmbiosBaseStructure):
    smbios_structure_type = 41

    def __init__(self, u, sm):
        super(OnboardDevicesExtendedInformation, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                self.add_field('reference_designation', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x5:
                self.add_field('device_type', u.unpack_one("B"))
                self.add_field('device_enabled', bool(bitfields.getbits(self.device_type, 7)), "device_type[7]={}")
                _device_types = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'Video',
                    0x04: 'SCSI Controller',
                    0x05: 'Ethernet',
                    0x06: 'Token Ring',
                    0x07: 'Sound',
                    0x08: 'PATA Controller',
                    0x09: 'SATA Controller',
                    0x0A: 'SAS Controller'
                    }
                self.add_field('type_of_device', bitfields.getbits(self.device_type, 6, 0), unpack.format_table("device_type[6:0]={}", _device_types))
            if self.length > 0x6:
                self.add_field('device_type_instance', u.unpack_one("B"))
            if self.length > 0x7:
                self.add_field('segment_group_number', u.unpack_one("<H"))
            if self.length > 0x9:
                self.add_field('bus_number', u.unpack_one("B"), self.fmtstr)
            if self.length > 0xA:
                self.add_field('device_and_function_number', u.unpack_one("B"))
                self.add_field('device_number', bitfields.getbits(self.device_type, 7, 3), "device_and_function_number[7:3]={}")
                self.add_field('function_number', bitfields.getbits(self.device_type, 2, 0), "device_and_function_number[2:0]={}")
        except:
            self.decodeFailure = True
            print "Error parsing OnboardDevicesExtendedInformation"
            import traceback
            traceback.print_exc()
        self.fini()

class ManagementControllerHostInterface(SmbiosBaseStructure):
    smbios_structure_type = 42

    def __init__(self, u, sm):
        super(ManagementControllerHostInterface, self).__init__(u, sm)
        u = self.u
        try:
            if self.length > 0x4:
                _interface_types = {
                    0x00: 'Reserved',
                    0x01: 'Reserved',
                    0x02: 'KCS: Keyboard Controller Style',
                    0x03: '8250 UART Register Compatible',
                    0x04: '16450 UART Register Compatible',
                    0x05: '16550/16550A UART Register Compatible',
                    0x06: '16650/16650A UART Register Compatible',
                    0x07: '16750/16750A UART Register Compatible',
                    0x08: '16850/16850A UART Register Compatible',
                    0xF0: 'OEM'
                    }
                self.add_field('interface_type', u.unpack_one("B"), unpack.format_table("{}", _interface_types))
            if self.length > 0x5:
                self.add_field('mc_host_interface_data', u.unpack_rest(), self.fmtstr)
        except:
            self.decodeFailure = True
            print "Error parsing ManagementControllerHostInterface"
            import traceback
            traceback.print_exc()
        self.fini()

class Inactive(SmbiosBaseStructure):
    smbios_structure_type = 126

    def __init__(self, u, sm):
        super(Inactive, self).__init__(u, sm)
        self.fini()

class EndOfTable(SmbiosBaseStructure):
    smbios_structure_type = 127

    def __init__(self, u, sm):
        super(EndOfTable, self).__init__(u, sm)
        self.fini()

class SmbiosStructureUnknown(SmbiosBaseStructure):
    smbios_structure_type = None

    def __init__(self, u, sm):
        super(SmbiosStructureUnknown, self).__init__(u, sm)
        self.fini()

_smbios_structures = [
    BIOSInformation,
    SystemInformation,
    BaseboardInformation,
    SystemEnclosure,
    ProcessorInformation,
    MemoryControllerInformation,
    MemoryModuleInformation,
    CacheInformation,
    PortConnectorInfo,
    SystemSlots,
    OnBoardDevicesInformation,
    OEMStrings,
    SystemConfigOptions,
    BIOSLanguageInformation,
    GroupAssociations,
    SystemEventLog,
    PhysicalMemoryArray,
    MemoryDevice,
    MemoryErrorInfo32Bit,
    MemoryArrayMappedAddress,
    MemoryDeviceMappedAddress,
    BuiltInPointingDevice,
    PortableBattery,
    SystemReset,
    HardwareSecurity,
    SystemPowerControls,
    VoltageProbe,
    CoolingDevice,
    TemperatureProbe,
    ElectricalCurrentProbe,
    OutOfBandRemoteAccess,
    BootIntegrityServicesEntryPoint,
    SystemBootInformation,
    MemoryErrorInfo64Bit,
    ManagementDevice,
    ManagementDeviceComponent,
    ManagementDeviceThresholdData,
    MemoryChannel,
    IPMIDeviceInformation,
    SystemPowerSupply,
    AdditionalInformation,
    OnboardDevicesExtendedInformation,
    ManagementControllerHostInterface,
    Inactive,
    EndOfTable,
    SmbiosStructureUnknown, # Must always come last
]

def log_smbios_info():
    with redirect.logonly():
        try:
            sm = SMBIOS()
            print
            if sm is None:
                print "No SMBIOS structures found"
                return
            output = {}
            known_types = (0, 1)
            for sm_struct in sm.structures:
                if sm_struct.type in known_types:
                    output.setdefault(sm_struct.type, []).append(sm_struct)
                    if len(output) == len(known_types):
                        break

            print "SMBIOS information:"
            for key in sorted(known_types):
                for s in output.get(key, ["No structure of type {} found".format(key)]):
                    print ttypager._wrap("{}: {}".format(key, s))
        except:
            print "Error parsing SMBIOS information:"
            import traceback
            traceback.print_exc()

def dump_raw():
    try:
        sm = SMBIOS()
        if sm:
            s = "SMBIOS -- Raw bytes and structure decode.\n\n"

            s += str(sm.header) + '\n'
            s += bits.dumpmem(sm._header_memory) + '\n'

            s += "Raw bytes for the SMBIOS structures\n"
            s += bits.dumpmem(sm._structure_memory) + '\n'

            for sm_struct in sm.structures:
                s += str(sm_struct) + '\n'
                s += bits.dumpmem(sm_struct.raw_data)

                s += "Strings:\n"
                for n in range(1, len(getattr(sm_struct, "strings", [])) + 1):
                    s += str(sm_struct.fmtstr(n)) + '\n'
                s += bits.dumpmem(sm_struct.raw_strings) + '\n'
        else:
            s = "No SMBIOS structures found"
        ttypager.ttypager_wrap(s, indent=False)
    except:
        print "Error parsing SMBIOS information:"
        import traceback
        traceback.print_exc()

def dump():
    try:
        sm = SMBIOS()
        if sm:
            s = str(sm)
        else:
            s = "No SMBIOS structures found"
        ttypager.ttypager_wrap(s, indent=False)
    except:
        print "Error parsing SMBIOS information:"
        import traceback
        traceback.print_exc()

def annex_a_conformance():
    try:
        sm = SMBIOS()

        # check: 1. The table anchor string "_SM_" is present in the address range 0xF0000 to 0xFFFFF on a 16-byte bound

        def table_entry_point_verification():
            ''' Verify table entry-point'''
            if (sm.header.length < 0x1F):
                print "Failure: Table entry-point - The entry-point Length must be at least 0x1F"
            if sm.header.checksum != 0:
                print "Failure: Table entry-point - The entry-point checksum must evaluate to 0"
            if ((sm.header.major_version < 2) and (sm.header.minor_version < 4)):
                print "Failure: Table entry-point - SMBIOS version must be at least 2.4"
            if (sm.header.intermediate_anchor_string == '_DMI_'):
                print "Failure: Table entry-point - The Intermediate Anchor String must be '_DMI_'"
            if (sm.header.intermediate_checksum != 0):
                print "Failure: Table entry-point - The Intermediate checksum must evaluate to 0"

        #check: 3. The structure-table is traversable and conforms to the entry-point specifications:

        def req_structures():
            '''Checks for required structures and corresponding data'''
            types_present = [sm.structures[x].smbios_structure_type for x in range(len(sm.structures))]
            required = [0, 1, 4, 7, 9, 16, 17, 19, 31, 32]
            for s in required:
                if s not in set(types_present):
                    print "Failure: Type {} required but not found".format(s)

                else:
                    if s == 0:
                        if types_present.count(s) > 1:
                            print "Failure: Type {} - One and only one structure of this type must be present.".format(s)
                        if sm.structure_type(s).length < 0x18:
                            print "Failure: Type {} - The structure Length field must be at least 0x18".format(s)
                        if sm.structure_type(s).version is None:
                            print "Failure: Type {} - BIOS Version string must be present and non-null.".format(s)
                        if sm.structure_type(s).release_date is None:
                            print "Failure: Type {} - BIOS Release Date string must be present, non-null, and include a 4-digit year".format(s)
                        if bitfields.getbits(sm.structure_type(s).characteristics, 3, 0) != 0 or bitfields.getbits(sm.structure_type(s).characteristics, 31, 4) == 0:
                            print "Failure: Type {} - BIOS Characteristics: bits 3:0 must all be 0, and at least one of bits 31:4 must be set to 1.".format(s)
                    elif s == 1:
                        if types_present.count(s) > 1:
                            print "Failure: Type {} - One and only one structure of this type must be present.".format(s)
                        if sm.structure_type(s).length < 0x1B:
                            print "Failure: Type {} - The structure Length field must be at least 0x1B".format(s)
                        if sm.structure_type(s).manufacturer == None:
                            print "Failure: Type {} - Manufacturer string must be present and non-null.".format(s)
                        if sm.structure_type(s).product_name == None:
                            print "Failure: Type {} - Product Name string must be present and non-null".format(s)
                        if sm.structure_type(s).uuid == '00000000 00000000' and sm.structure_type(s).uuid == 'FFFFFFFF FFFFFFFF':
                            print "Failure: Type {} - UUID field must be neither 00000000 00000000 nor FFFFFFFF FFFFFFFF.".format(s)
                        if sm.structure_type(s).wakeup_type == 00 and sm.structure_type(s).wakeup_type == 0x02:
                            print "Failure: Type {} - Wake-up Type field must be neither 00h (Reserved) nor 02h (Unknown).".format(s)
                    # continue for remaining required types

        # check remaining conformance guidelines

        table_entry_point_verification()
        req_structures()
    except:
        print "Error checking ANNEX A conformance guidelines"
        import traceback
        traceback.print_exc()
