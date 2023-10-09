#!/usr/bin/env python3
#
#  Migration Stream Analyzer
#
#  Copyright (c) 2015 Alexander Graf <agraf@suse.de>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.

import json
import os
import argparse
import collections
import struct
import sys


def mkdir_p(path):
    try:
        os.makedirs(path)
    except OSError:
        pass


class MigrationFile(object):
    def __init__(self, filename):
        self.filename = filename
        self.file = open(self.filename, "rb")

    def read64(self):
        return int.from_bytes(self.file.read(8), byteorder='big', signed=False)

    def read32(self):
        return int.from_bytes(self.file.read(4), byteorder='big', signed=False)

    def read16(self):
        return int.from_bytes(self.file.read(2), byteorder='big', signed=False)

    def read8(self):
        return int.from_bytes(self.file.read(1), byteorder='big', signed=True)

    def readstr(self, len = None):
        return self.readvar(len).decode('utf-8')

    def readvar(self, size = None):
        if size is None:
            size = self.read8()
        if size == 0:
            return ""
        value = self.file.read(size)
        if len(value) != size:
            raise Exception("Unexpected end of %s at 0x%x" % (self.filename, self.file.tell()))
        return value

    def tell(self):
        return self.file.tell()

    # The VMSD description is at the end of the file, after EOF. Look for
    # the last NULL byte, then for the beginning brace of JSON.
    def read_migration_debug_json(self):
        QEMU_VM_VMDESCRIPTION = 0x06

        # Remember the offset in the file when we started
        entrypos = self.file.tell()

        # Read the last 10MB
        self.file.seek(0, os.SEEK_END)
        endpos = self.file.tell()
        self.file.seek(max(-endpos, -10 * 1024 * 1024), os.SEEK_END)
        datapos = self.file.tell()
        data = self.file.read()
        # The full file read closed the file as well, reopen it
        self.file = open(self.filename, "rb")

        # Find the last NULL byte, then the first brace after that. This should
        # be the beginning of our JSON data.
        nulpos = data.rfind(b'\0')
        jsonpos = data.find(b'{', nulpos)

        # Check backwards from there and see whether we guessed right
        self.file.seek(datapos + jsonpos - 5, 0)
        if self.read8() != QEMU_VM_VMDESCRIPTION:
            raise Exception("No Debug Migration device found")

        jsonlen = self.read32()

        # Seek back to where we were at the beginning
        self.file.seek(entrypos, 0)

        # explicit decode() needed for Python 3.5 compatibility
        return data[jsonpos:jsonpos + jsonlen].decode("utf-8")

    def close(self):
        self.file.close()

class RamSection(object):
    RAM_SAVE_FLAG_COMPRESS = 0x02
    RAM_SAVE_FLAG_MEM_SIZE = 0x04
    RAM_SAVE_FLAG_PAGE     = 0x08
    RAM_SAVE_FLAG_EOS      = 0x10
    RAM_SAVE_FLAG_CONTINUE = 0x20
    RAM_SAVE_FLAG_XBZRLE   = 0x40
    RAM_SAVE_FLAG_HOOK     = 0x80
    RAM_SAVE_FLAG_COMPRESS_PAGE = 0x100
    RAM_SAVE_FLAG_MULTIFD_FLUSH = 0x200

    def __init__(self, file, version_id, ramargs, section_key):
        if version_id != 4:
            raise Exception("Unknown RAM version %d" % version_id)

        self.file = file
        self.section_key = section_key
        self.TARGET_PAGE_SIZE = ramargs['page_size']
        self.dump_memory = ramargs['dump_memory']
        self.write_memory = ramargs['write_memory']
        self.ignore_shared = ramargs['ignore_shared']
        self.sizeinfo = collections.OrderedDict()
        self.data = collections.OrderedDict()
        self.data['section sizes'] = self.sizeinfo
        self.name = ''
        if self.write_memory:
            self.files = { }
        if self.dump_memory:
            self.memory = collections.OrderedDict()
            self.data['memory'] = self.memory

    def __repr__(self):
        return self.data.__repr__()

    def __str__(self):
        return self.data.__str__()

    def getDict(self):
        return self.data

    def read(self):
        # Read all RAM sections
        while True:
            addr = self.file.read64()
            flags = addr & (self.TARGET_PAGE_SIZE - 1)
            addr &= ~(self.TARGET_PAGE_SIZE - 1)

            if flags & self.RAM_SAVE_FLAG_MEM_SIZE:
                while True:
                    namelen = self.file.read8()
                    # We assume that no RAM chunk is big enough to ever
                    # hit the first byte of the address, so when we see
                    # a zero here we know it has to be an address, not the
                    # length of the next block.
                    if namelen == 0:
                        self.file.file.seek(-1, 1)
                        break
                    self.name = self.file.readstr(len = namelen)
                    len = self.file.read64()
                    self.sizeinfo[self.name] = '0x%016x' % len
                    if self.write_memory:
                        print(self.name)
                        mkdir_p('./' + os.path.dirname(self.name))
                        f = open('./' + self.name, "wb")
                        f.truncate(0)
                        f.truncate(len)
                        self.files[self.name] = f
                    if self.ignore_shared:
                        mr_addr = self.file.read64()
                flags &= ~self.RAM_SAVE_FLAG_MEM_SIZE

            if flags & self.RAM_SAVE_FLAG_COMPRESS:
                if flags & self.RAM_SAVE_FLAG_CONTINUE:
                    flags &= ~self.RAM_SAVE_FLAG_CONTINUE
                else:
                    self.name = self.file.readstr()
                fill_char = self.file.read8()
                # The page in question is filled with fill_char now
                if self.write_memory and fill_char != 0:
                    self.files[self.name].seek(addr, os.SEEK_SET)
                    self.files[self.name].write(chr(fill_char) * self.TARGET_PAGE_SIZE)
                if self.dump_memory:
                    self.memory['%s (0x%016x)' % (self.name, addr)] = 'Filled with 0x%02x' % fill_char
                flags &= ~self.RAM_SAVE_FLAG_COMPRESS
            elif flags & self.RAM_SAVE_FLAG_PAGE:
                if flags & self.RAM_SAVE_FLAG_CONTINUE:
                    flags &= ~self.RAM_SAVE_FLAG_CONTINUE
                else:
                    self.name = self.file.readstr()

                if self.write_memory or self.dump_memory:
                    data = self.file.readvar(size = self.TARGET_PAGE_SIZE)
                else: # Just skip RAM data
                    self.file.file.seek(self.TARGET_PAGE_SIZE, 1)

                if self.write_memory:
                    self.files[self.name].seek(addr, os.SEEK_SET)
                    self.files[self.name].write(data)
                if self.dump_memory:
                    hexdata = " ".join("{0:02x}".format(ord(c)) for c in data)
                    self.memory['%s (0x%016x)' % (self.name, addr)] = hexdata

                flags &= ~self.RAM_SAVE_FLAG_PAGE
            elif flags & self.RAM_SAVE_FLAG_XBZRLE:
                raise Exception("XBZRLE RAM compression is not supported yet")
            elif flags & self.RAM_SAVE_FLAG_HOOK:
                raise Exception("RAM hooks don't make sense with files")
            if flags & self.RAM_SAVE_FLAG_MULTIFD_FLUSH:
                continue

            # End of RAM section
            if flags & self.RAM_SAVE_FLAG_EOS:
                break

            if flags != 0:
                raise Exception("Unknown RAM flags: %x" % flags)

    def __del__(self):
        if self.write_memory:
            for key in self.files:
                self.files[key].close()


class HTABSection(object):
    HASH_PTE_SIZE_64       = 16

    def __init__(self, file, version_id, device, section_key):
        if version_id != 1:
            raise Exception("Unknown HTAB version %d" % version_id)

        self.file = file
        self.section_key = section_key

    def read(self):

        header = self.file.read32()

        if (header == -1):
            # "no HPT" encoding
            return

        if (header > 0):
            # First section, just the hash shift
            return

        # Read until end marker
        while True:
            index = self.file.read32()
            n_valid = self.file.read16()
            n_invalid = self.file.read16()

            if index == 0 and n_valid == 0 and n_invalid == 0:
                break

            self.file.readvar(n_valid * self.HASH_PTE_SIZE_64)

    def getDict(self):
        return ""


class ConfigurationSection(object):
    def __init__(self, file, desc):
        self.file = file
        self.desc = desc
        self.caps = []

    def parse_capabilities(self, vmsd_caps):
        if not vmsd_caps:
            return

        ncaps = vmsd_caps.data['caps_count'].data
        self.caps = vmsd_caps.data['capabilities']

        if type(self.caps) != list:
            self.caps = [self.caps]

        if len(self.caps) != ncaps:
            raise Exception("Number of capabilities doesn't match "
                            "caps_count field")

    def has_capability(self, cap):
        return any([str(c) == cap for c in self.caps])

    def read(self):
        if self.desc:
            version_id = self.desc['version']
            section = VMSDSection(self.file, version_id, self.desc,
                                  'configuration')
            section.read()
            self.parse_capabilities(
                section.data.get("configuration/capabilities"))
        else:
            # backward compatibility for older streams that don't have
            # the configuration section in the json
            name_len = self.file.read32()
            name = self.file.readstr(len = name_len)

class VMSDFieldGeneric(object):
    def __init__(self, desc, file):
        self.file = file
        self.desc = desc
        self.data = ""

    def __repr__(self):
        return str(self.__str__())

    def __str__(self):
        return " ".join("{0:02x}".format(c) for c in self.data)

    def getDict(self):
        return self.__str__()

    def read(self):
        size = int(self.desc['size'])
        self.data = self.file.readvar(size)
        return self.data

class VMSDFieldCap(object):
    def __init__(self, desc, file):
        self.file = file
        self.desc = desc
        self.data = ""

    def __repr__(self):
        return self.data

    def __str__(self):
        return self.data

    def read(self):
        len = self.file.read8()
        self.data = self.file.readstr(len)


class VMSDFieldInt(VMSDFieldGeneric):
    def __init__(self, desc, file):
        super(VMSDFieldInt, self).__init__(desc, file)
        self.size = int(desc['size'])
        self.format = '0x%%0%dx' % (self.size * 2)
        self.sdtype = '>i%d' % self.size
        self.udtype = '>u%d' % self.size

    def __repr__(self):
        if self.data < 0:
            return ('%s (%d)' % ((self.format % self.udata), self.data))
        else:
            return self.format % self.data

    def __str__(self):
        return self.__repr__()

    def getDict(self):
        return self.__str__()

    def read(self):
        super(VMSDFieldInt, self).read()
        self.sdata = int.from_bytes(self.data, byteorder='big', signed=True)
        self.udata = int.from_bytes(self.data, byteorder='big', signed=False)
        self.data = self.sdata
        return self.data

class VMSDFieldUInt(VMSDFieldInt):
    def __init__(self, desc, file):
        super(VMSDFieldUInt, self).__init__(desc, file)

    def read(self):
        super(VMSDFieldUInt, self).read()
        self.data = self.udata
        return self.data

class VMSDFieldIntLE(VMSDFieldInt):
    def __init__(self, desc, file):
        super(VMSDFieldIntLE, self).__init__(desc, file)
        self.dtype = '<i%d' % self.size

class VMSDFieldBool(VMSDFieldGeneric):
    def __init__(self, desc, file):
        super(VMSDFieldBool, self).__init__(desc, file)

    def __repr__(self):
        return self.data.__repr__()

    def __str__(self):
        return self.data.__str__()

    def getDict(self):
        return self.data

    def read(self):
        super(VMSDFieldBool, self).read()
        if self.data[0] == 0:
            self.data = False
        else:
            self.data = True
        return self.data

class VMSDFieldStruct(VMSDFieldGeneric):
    QEMU_VM_SUBSECTION    = 0x05

    def __init__(self, desc, file):
        super(VMSDFieldStruct, self).__init__(desc, file)
        self.data = collections.OrderedDict()

        # When we see compressed array elements, unfold them here
        new_fields = []
        for field in self.desc['struct']['fields']:
            if not 'array_len' in field:
                new_fields.append(field)
                continue
            array_len = field.pop('array_len')
            field['index'] = 0
            new_fields.append(field)
            for i in range(1, array_len):
                c = field.copy()
                c['index'] = i
                new_fields.append(c)

        self.desc['struct']['fields'] = new_fields

    def __repr__(self):
        return self.data.__repr__()

    def __str__(self):
        return self.data.__str__()

    def read(self):
        for field in self.desc['struct']['fields']:
            try:
                reader = vmsd_field_readers[field['type']]
            except:
                reader = VMSDFieldGeneric

            field['data'] = reader(field, self.file)
            field['data'].read()

            if 'index' in field:
                if field['name'] not in self.data:
                    self.data[field['name']] = []
                a = self.data[field['name']]
                if len(a) != int(field['index']):
                    raise Exception("internal index of data field unmatched (%d/%d)" % (len(a), int(field['index'])))
                a.append(field['data'])
            else:
                self.data[field['name']] = field['data']

        if 'subsections' in self.desc['struct']:
            for subsection in self.desc['struct']['subsections']:
                if self.file.read8() != self.QEMU_VM_SUBSECTION:
                    raise Exception("Subsection %s not found at offset %x" % ( subsection['vmsd_name'], self.file.tell()))
                name = self.file.readstr()
                version_id = self.file.read32()
                self.data[name] = VMSDSection(self.file, version_id, subsection, (name, 0))
                self.data[name].read()

    def getDictItem(self, value):
       # Strings would fall into the array category, treat
       # them specially
       if value.__class__ is ''.__class__:
           return value

       try:
           return self.getDictOrderedDict(value)
       except:
           try:
               return self.getDictArray(value)
           except:
               try:
                   return value.getDict()
               except:
                   return value

    def getDictArray(self, array):
        r = []
        for value in array:
           r.append(self.getDictItem(value))
        return r

    def getDictOrderedDict(self, dict):
        r = collections.OrderedDict()
        for (key, value) in dict.items():
            r[key] = self.getDictItem(value)
        return r

    def getDict(self):
        return self.getDictOrderedDict(self.data)

vmsd_field_readers = {
    "bool" : VMSDFieldBool,
    "int8" : VMSDFieldInt,
    "int16" : VMSDFieldInt,
    "int32" : VMSDFieldInt,
    "int32 equal" : VMSDFieldInt,
    "int32 le" : VMSDFieldIntLE,
    "int64" : VMSDFieldInt,
    "uint8" : VMSDFieldUInt,
    "uint16" : VMSDFieldUInt,
    "uint32" : VMSDFieldUInt,
    "uint32 equal" : VMSDFieldUInt,
    "uint64" : VMSDFieldUInt,
    "int64 equal" : VMSDFieldInt,
    "uint8 equal" : VMSDFieldInt,
    "uint16 equal" : VMSDFieldInt,
    "float64" : VMSDFieldGeneric,
    "timer" : VMSDFieldGeneric,
    "buffer" : VMSDFieldGeneric,
    "unused_buffer" : VMSDFieldGeneric,
    "bitmap" : VMSDFieldGeneric,
    "struct" : VMSDFieldStruct,
    "capability": VMSDFieldCap,
    "unknown" : VMSDFieldGeneric,
}

class VMSDSection(VMSDFieldStruct):
    def __init__(self, file, version_id, device, section_key):
        self.file = file
        self.data = ""
        self.vmsd_name = ""
        self.section_key = section_key
        desc = device
        if 'vmsd_name' in device:
            self.vmsd_name = device['vmsd_name']

        # A section really is nothing but a FieldStruct :)
        super(VMSDSection, self).__init__({ 'struct' : desc }, file)

###############################################################################

class MigrationDump(object):
    QEMU_VM_FILE_MAGIC    = 0x5145564d
    QEMU_VM_FILE_VERSION  = 0x00000003
    QEMU_VM_EOF           = 0x00
    QEMU_VM_SECTION_START = 0x01
    QEMU_VM_SECTION_PART  = 0x02
    QEMU_VM_SECTION_END   = 0x03
    QEMU_VM_SECTION_FULL  = 0x04
    QEMU_VM_SUBSECTION    = 0x05
    QEMU_VM_VMDESCRIPTION = 0x06
    QEMU_VM_CONFIGURATION = 0x07
    QEMU_VM_SECTION_FOOTER= 0x7e

    def __init__(self, filename):
        self.section_classes = { ( 'ram', 0 ) : [ RamSection, None ],
                                 ( 'spapr/htab', 0) : ( HTABSection, None ) }
        self.filename = filename
        self.vmsd_desc = None

    def read(self, desc_only = False, dump_memory = False, write_memory = False):
        # Read in the whole file
        file = MigrationFile(self.filename)

        # File magic
        data = file.read32()
        if data != self.QEMU_VM_FILE_MAGIC:
            raise Exception("Invalid file magic %x" % data)

        # Version (has to be v3)
        data = file.read32()
        if data != self.QEMU_VM_FILE_VERSION:
            raise Exception("Invalid version number %d" % data)

        self.load_vmsd_json(file)

        # Read sections
        self.sections = collections.OrderedDict()

        if desc_only:
            return

        ramargs = {}
        ramargs['page_size'] = self.vmsd_desc['page_size']
        ramargs['dump_memory'] = dump_memory
        ramargs['write_memory'] = write_memory
        ramargs['ignore_shared'] = False
        self.section_classes[('ram',0)][1] = ramargs

        while True:
            section_type = file.read8()
            if section_type == self.QEMU_VM_EOF:
                break
            elif section_type == self.QEMU_VM_CONFIGURATION:
                config_desc = self.vmsd_desc.get('configuration')
                section = ConfigurationSection(file, config_desc)
                section.read()
                ramargs['ignore_shared'] = section.has_capability('x-ignore-shared')
            elif section_type == self.QEMU_VM_SECTION_START or section_type == self.QEMU_VM_SECTION_FULL:
                section_id = file.read32()
                name = file.readstr()
                instance_id = file.read32()
                version_id = file.read32()
                section_key = (name, instance_id)
                classdesc = self.section_classes[section_key]
                section = classdesc[0](file, version_id, classdesc[1], section_key)
                self.sections[section_id] = section
                section.read()
            elif section_type == self.QEMU_VM_SECTION_PART or section_type == self.QEMU_VM_SECTION_END:
                section_id = file.read32()
                self.sections[section_id].read()
            elif section_type == self.QEMU_VM_SECTION_FOOTER:
                read_section_id = file.read32()
                if read_section_id != section_id:
                    raise Exception("Mismatched section footer: %x vs %x" % (read_section_id, section_id))
            else:
                raise Exception("Unknown section type: %d" % section_type)
        file.close()

    def load_vmsd_json(self, file):
        vmsd_json = file.read_migration_debug_json()
        self.vmsd_desc = json.loads(vmsd_json, object_pairs_hook=collections.OrderedDict)
        for device in self.vmsd_desc['devices']:
            key = (device['name'], device['instance_id'])
            value = ( VMSDSection, device )
            self.section_classes[key] = value

    def getDict(self):
        r = collections.OrderedDict()
        for (key, value) in self.sections.items():
           key = "%s (%d)" % ( value.section_key[0], key )
           r[key] = value.getDict()
        return r

###############################################################################

class JSONEncoder(json.JSONEncoder):
    def default(self, o):
        if isinstance(o, VMSDFieldGeneric):
            return str(o)
        return json.JSONEncoder.default(self, o)

parser = argparse.ArgumentParser()
parser.add_argument("-f", "--file", help='migration dump to read from', required=True)
parser.add_argument("-m", "--memory", help='dump RAM contents as well', action='store_true')
parser.add_argument("-d", "--dump", help='what to dump ("state" or "desc")', default='state')
parser.add_argument("-x", "--extract", help='extract contents into individual files', action='store_true')
args = parser.parse_args()

jsonenc = JSONEncoder(indent=4, separators=(',', ': '))

if args.extract:
    dump = MigrationDump(args.file)

    dump.read(desc_only = True)
    print("desc.json")
    f = open("desc.json", "w")
    f.truncate()
    f.write(jsonenc.encode(dump.vmsd_desc))
    f.close()

    dump.read(write_memory = True)
    dict = dump.getDict()
    print("state.json")
    f = open("state.json", "w")
    f.truncate()
    f.write(jsonenc.encode(dict))
    f.close()
elif args.dump == "state":
    dump = MigrationDump(args.file)
    dump.read(dump_memory = args.memory)
    dict = dump.getDict()
    print(jsonenc.encode(dict))
elif args.dump == "desc":
    dump = MigrationDump(args.file)
    dump.read(desc_only = True)
    print(jsonenc.encode(dump.vmsd_desc))
else:
    raise Exception("Please specify either -x, -d state or -d desc")
