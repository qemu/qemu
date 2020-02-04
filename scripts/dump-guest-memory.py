"""
This python script adds a new gdb command, "dump-guest-memory". It
should be loaded with "source dump-guest-memory.py" at the (gdb)
prompt.

Copyright (C) 2013, Red Hat, Inc.

Authors:
   Laszlo Ersek <lersek@redhat.com>
   Janosch Frank <frankja@linux.vnet.ibm.com>

This work is licensed under the terms of the GNU GPL, version 2 or later. See
the COPYING file in the top-level directory.
"""

import ctypes
import struct

try:
    UINTPTR_T = gdb.lookup_type("uintptr_t")
except Exception as inst:
    raise gdb.GdbError("Symbols must be loaded prior to sourcing dump-guest-memory.\n"
                       "Symbols may be loaded by 'attach'ing a QEMU process id or by "
                       "'load'ing a QEMU binary.")

TARGET_PAGE_SIZE = 0x1000
TARGET_PAGE_MASK = 0xFFFFFFFFFFFFF000

# Special value for e_phnum. This indicates that the real number of
# program headers is too large to fit into e_phnum. Instead the real
# value is in the field sh_info of section 0.
PN_XNUM = 0xFFFF

EV_CURRENT = 1

ELFCLASS32 = 1
ELFCLASS64 = 2

ELFDATA2LSB = 1
ELFDATA2MSB = 2

ET_CORE = 4

PT_LOAD = 1
PT_NOTE = 4

EM_386 = 3
EM_PPC = 20
EM_PPC64 = 21
EM_S390 = 22
EM_AARCH = 183
EM_X86_64 = 62

VMCOREINFO_FORMAT_ELF = 1

def le16_to_cpu(val):
    return struct.unpack("<H", struct.pack("=H", val))[0]

def le32_to_cpu(val):
    return struct.unpack("<I", struct.pack("=I", val))[0]

def le64_to_cpu(val):
    return struct.unpack("<Q", struct.pack("=Q", val))[0]

class ELF(object):
    """Representation of a ELF file."""

    def __init__(self, arch):
        self.ehdr = None
        self.notes = []
        self.segments = []
        self.notes_size = 0
        self.endianness = None
        self.elfclass = ELFCLASS64

        if arch == 'aarch64-le':
            self.endianness = ELFDATA2LSB
            self.elfclass = ELFCLASS64
            self.ehdr = get_arch_ehdr(self.endianness, self.elfclass)
            self.ehdr.e_machine = EM_AARCH

        elif arch == 'aarch64-be':
            self.endianness = ELFDATA2MSB
            self.ehdr = get_arch_ehdr(self.endianness, self.elfclass)
            self.ehdr.e_machine = EM_AARCH

        elif arch == 'X86_64':
            self.endianness = ELFDATA2LSB
            self.ehdr = get_arch_ehdr(self.endianness, self.elfclass)
            self.ehdr.e_machine = EM_X86_64

        elif arch == '386':
            self.endianness = ELFDATA2LSB
            self.elfclass = ELFCLASS32
            self.ehdr = get_arch_ehdr(self.endianness, self.elfclass)
            self.ehdr.e_machine = EM_386

        elif arch == 's390':
            self.endianness = ELFDATA2MSB
            self.ehdr = get_arch_ehdr(self.endianness, self.elfclass)
            self.ehdr.e_machine = EM_S390

        elif arch == 'ppc64-le':
            self.endianness = ELFDATA2LSB
            self.ehdr = get_arch_ehdr(self.endianness, self.elfclass)
            self.ehdr.e_machine = EM_PPC64

        elif arch == 'ppc64-be':
            self.endianness = ELFDATA2MSB
            self.ehdr = get_arch_ehdr(self.endianness, self.elfclass)
            self.ehdr.e_machine = EM_PPC64

        else:
            raise gdb.GdbError("No valid arch type specified.\n"
                               "Currently supported types:\n"
                               "aarch64-be, aarch64-le, X86_64, 386, s390, "
                               "ppc64-be, ppc64-le")

        self.add_segment(PT_NOTE, 0, 0)

    def add_note(self, n_name, n_desc, n_type):
        """Adds a note to the ELF."""

        note = get_arch_note(self.endianness, len(n_name), len(n_desc))
        note.n_namesz = len(n_name) + 1
        note.n_descsz = len(n_desc)
        note.n_name = n_name.encode()
        note.n_type = n_type

        # Desc needs to be 4 byte aligned (although the 64bit spec
        # specifies 8 byte). When defining n_desc as uint32 it will be
        # automatically aligned but we need the memmove to copy the
        # string into it.
        ctypes.memmove(note.n_desc, n_desc.encode(), len(n_desc))

        self.notes.append(note)
        self.segments[0].p_filesz += ctypes.sizeof(note)
        self.segments[0].p_memsz += ctypes.sizeof(note)


    def add_vmcoreinfo_note(self, vmcoreinfo):
        """Adds a vmcoreinfo note to the ELF dump."""
        # compute the header size, and copy that many bytes from the note
        header = get_arch_note(self.endianness, 0, 0)
        ctypes.memmove(ctypes.pointer(header),
                       vmcoreinfo, ctypes.sizeof(header))
        if header.n_descsz > 1 << 20:
            print('warning: invalid vmcoreinfo size')
            return
        # now get the full note
        note = get_arch_note(self.endianness,
                             header.n_namesz - 1, header.n_descsz)
        ctypes.memmove(ctypes.pointer(note), vmcoreinfo, ctypes.sizeof(note))

        self.notes.append(note)
        self.segments[0].p_filesz += ctypes.sizeof(note)
        self.segments[0].p_memsz += ctypes.sizeof(note)

    def add_segment(self, p_type, p_paddr, p_size):
        """Adds a segment to the elf."""

        phdr = get_arch_phdr(self.endianness, self.elfclass)
        phdr.p_type = p_type
        phdr.p_paddr = p_paddr
        phdr.p_vaddr = p_paddr
        phdr.p_filesz = p_size
        phdr.p_memsz = p_size
        self.segments.append(phdr)
        self.ehdr.e_phnum += 1

    def to_file(self, elf_file):
        """Writes all ELF structures to the passed file.

        Structure:
        Ehdr
        Segment 0:PT_NOTE
        Segment 1:PT_LOAD
        Segment N:PT_LOAD
        Note    0..N
        Dump contents
        """
        elf_file.write(self.ehdr)
        off = ctypes.sizeof(self.ehdr) + \
              len(self.segments) * ctypes.sizeof(self.segments[0])

        for phdr in self.segments:
            phdr.p_offset = off
            elf_file.write(phdr)
            off += phdr.p_filesz

        for note in self.notes:
            elf_file.write(note)


def get_arch_note(endianness, len_name, len_desc):
    """Returns a Note class with the specified endianness."""

    if endianness == ELFDATA2LSB:
        superclass = ctypes.LittleEndianStructure
    else:
        superclass = ctypes.BigEndianStructure

    len_name = len_name + 1

    class Note(superclass):
        """Represents an ELF note, includes the content."""

        _fields_ = [("n_namesz", ctypes.c_uint32),
                    ("n_descsz", ctypes.c_uint32),
                    ("n_type", ctypes.c_uint32),
                    ("n_name", ctypes.c_char * len_name),
                    ("n_desc", ctypes.c_uint32 * ((len_desc + 3) // 4))]
    return Note()


class Ident(ctypes.Structure):
    """Represents the ELF ident array in the ehdr structure."""

    _fields_ = [('ei_mag0', ctypes.c_ubyte),
                ('ei_mag1', ctypes.c_ubyte),
                ('ei_mag2', ctypes.c_ubyte),
                ('ei_mag3', ctypes.c_ubyte),
                ('ei_class', ctypes.c_ubyte),
                ('ei_data', ctypes.c_ubyte),
                ('ei_version', ctypes.c_ubyte),
                ('ei_osabi', ctypes.c_ubyte),
                ('ei_abiversion', ctypes.c_ubyte),
                ('ei_pad', ctypes.c_ubyte * 7)]

    def __init__(self, endianness, elfclass):
        self.ei_mag0 = 0x7F
        self.ei_mag1 = ord('E')
        self.ei_mag2 = ord('L')
        self.ei_mag3 = ord('F')
        self.ei_class = elfclass
        self.ei_data = endianness
        self.ei_version = EV_CURRENT


def get_arch_ehdr(endianness, elfclass):
    """Returns a EHDR64 class with the specified endianness."""

    if endianness == ELFDATA2LSB:
        superclass = ctypes.LittleEndianStructure
    else:
        superclass = ctypes.BigEndianStructure

    class EHDR64(superclass):
        """Represents the 64 bit ELF header struct."""

        _fields_ = [('e_ident', Ident),
                    ('e_type', ctypes.c_uint16),
                    ('e_machine', ctypes.c_uint16),
                    ('e_version', ctypes.c_uint32),
                    ('e_entry', ctypes.c_uint64),
                    ('e_phoff', ctypes.c_uint64),
                    ('e_shoff', ctypes.c_uint64),
                    ('e_flags', ctypes.c_uint32),
                    ('e_ehsize', ctypes.c_uint16),
                    ('e_phentsize', ctypes.c_uint16),
                    ('e_phnum', ctypes.c_uint16),
                    ('e_shentsize', ctypes.c_uint16),
                    ('e_shnum', ctypes.c_uint16),
                    ('e_shstrndx', ctypes.c_uint16)]

        def __init__(self):
            super(superclass, self).__init__()
            self.e_ident = Ident(endianness, elfclass)
            self.e_type = ET_CORE
            self.e_version = EV_CURRENT
            self.e_ehsize = ctypes.sizeof(self)
            self.e_phoff = ctypes.sizeof(self)
            self.e_phentsize = ctypes.sizeof(get_arch_phdr(endianness, elfclass))
            self.e_phnum = 0


    class EHDR32(superclass):
        """Represents the 32 bit ELF header struct."""

        _fields_ = [('e_ident', Ident),
                    ('e_type', ctypes.c_uint16),
                    ('e_machine', ctypes.c_uint16),
                    ('e_version', ctypes.c_uint32),
                    ('e_entry', ctypes.c_uint32),
                    ('e_phoff', ctypes.c_uint32),
                    ('e_shoff', ctypes.c_uint32),
                    ('e_flags', ctypes.c_uint32),
                    ('e_ehsize', ctypes.c_uint16),
                    ('e_phentsize', ctypes.c_uint16),
                    ('e_phnum', ctypes.c_uint16),
                    ('e_shentsize', ctypes.c_uint16),
                    ('e_shnum', ctypes.c_uint16),
                    ('e_shstrndx', ctypes.c_uint16)]

        def __init__(self):
            super(superclass, self).__init__()
            self.e_ident = Ident(endianness, elfclass)
            self.e_type = ET_CORE
            self.e_version = EV_CURRENT
            self.e_ehsize = ctypes.sizeof(self)
            self.e_phoff = ctypes.sizeof(self)
            self.e_phentsize = ctypes.sizeof(get_arch_phdr(endianness, elfclass))
            self.e_phnum = 0

    # End get_arch_ehdr
    if elfclass == ELFCLASS64:
        return EHDR64()
    else:
        return EHDR32()


def get_arch_phdr(endianness, elfclass):
    """Returns a 32 or 64 bit PHDR class with the specified endianness."""

    if endianness == ELFDATA2LSB:
        superclass = ctypes.LittleEndianStructure
    else:
        superclass = ctypes.BigEndianStructure

    class PHDR64(superclass):
        """Represents the 64 bit ELF program header struct."""

        _fields_ = [('p_type', ctypes.c_uint32),
                    ('p_flags', ctypes.c_uint32),
                    ('p_offset', ctypes.c_uint64),
                    ('p_vaddr', ctypes.c_uint64),
                    ('p_paddr', ctypes.c_uint64),
                    ('p_filesz', ctypes.c_uint64),
                    ('p_memsz', ctypes.c_uint64),
                    ('p_align', ctypes.c_uint64)]

    class PHDR32(superclass):
        """Represents the 32 bit ELF program header struct."""

        _fields_ = [('p_type', ctypes.c_uint32),
                    ('p_offset', ctypes.c_uint32),
                    ('p_vaddr', ctypes.c_uint32),
                    ('p_paddr', ctypes.c_uint32),
                    ('p_filesz', ctypes.c_uint32),
                    ('p_memsz', ctypes.c_uint32),
                    ('p_flags', ctypes.c_uint32),
                    ('p_align', ctypes.c_uint32)]

    # End get_arch_phdr
    if elfclass == ELFCLASS64:
        return PHDR64()
    else:
        return PHDR32()


def int128_get64(val):
    """Returns low 64bit part of Int128 struct."""

    try:
        assert val["hi"] == 0
        return val["lo"]
    except gdb.error:
        u64t = gdb.lookup_type('uint64_t').array(2)
        u64 = val.cast(u64t)
        if sys.byteorder == 'little':
            assert u64[1] == 0
            return u64[0]
        else:
            assert u64[0] == 0
            return u64[1]


def qlist_foreach(head, field_str):
    """Generator for qlists."""

    var_p = head["lh_first"]
    while var_p != 0:
        var = var_p.dereference()
        var_p = var[field_str]["le_next"]
        yield var


def qemu_map_ram_ptr(block, offset):
    """Returns qemu vaddr for given guest physical address."""

    return block["host"] + offset


def memory_region_get_ram_ptr(memory_region):
    if memory_region["alias"] != 0:
        return (memory_region_get_ram_ptr(memory_region["alias"].dereference())
                + memory_region["alias_offset"])

    return qemu_map_ram_ptr(memory_region["ram_block"], 0)


def get_guest_phys_blocks():
    """Returns a list of ram blocks.

    Each block entry contains:
    'target_start': guest block phys start address
    'target_end':   guest block phys end address
    'host_addr':    qemu vaddr of the block's start
    """

    guest_phys_blocks = []

    print("guest RAM blocks:")
    print("target_start     target_end       host_addr        message "
          "count")
    print("---------------- ---------------- ---------------- ------- "
          "-----")

    current_map_p = gdb.parse_and_eval("address_space_memory.current_map")
    current_map = current_map_p.dereference()

    # Conversion to int is needed for python 3
    # compatibility. Otherwise range doesn't cast the value itself and
    # breaks.
    for cur in range(int(current_map["nr"])):
        flat_range = (current_map["ranges"] + cur).dereference()
        memory_region = flat_range["mr"].dereference()

        # we only care about RAM
        if (not memory_region["ram"] or
            memory_region["ram_device"] or
            memory_region["nonvolatile"]):
            continue

        section_size = int128_get64(flat_range["addr"]["size"])
        target_start = int128_get64(flat_range["addr"]["start"])
        target_end = target_start + section_size
        host_addr = (memory_region_get_ram_ptr(memory_region)
                     + flat_range["offset_in_region"])
        predecessor = None

        # find continuity in guest physical address space
        if len(guest_phys_blocks) > 0:
            predecessor = guest_phys_blocks[-1]
            predecessor_size = (predecessor["target_end"] -
                                predecessor["target_start"])

            # the memory API guarantees monotonically increasing
            # traversal
            assert predecessor["target_end"] <= target_start

            # we want continuity in both guest-physical and
            # host-virtual memory
            if (predecessor["target_end"] < target_start or
                predecessor["host_addr"] + predecessor_size != host_addr):
                predecessor = None

        if predecessor is None:
            # isolated mapping, add it to the list
            guest_phys_blocks.append({"target_start": target_start,
                                      "target_end":   target_end,
                                      "host_addr":    host_addr})
            message = "added"
        else:
            # expand predecessor until @target_end; predecessor's
            # start doesn't change
            predecessor["target_end"] = target_end
            message = "joined"

        print("%016x %016x %016x %-7s %5u" %
              (target_start, target_end, host_addr.cast(UINTPTR_T),
               message, len(guest_phys_blocks)))

    return guest_phys_blocks


# The leading docstring doesn't have idiomatic Python formatting. It is
# printed by gdb's "help" command (the first line is printed in the
# "help data" summary), and it should match how other help texts look in
# gdb.
class DumpGuestMemory(gdb.Command):
    """Extract guest vmcore from qemu process coredump.

The two required arguments are FILE and ARCH:
FILE identifies the target file to write the guest vmcore to.
ARCH specifies the architecture for which the core will be generated.

This GDB command reimplements the dump-guest-memory QMP command in
python, using the representation of guest memory as captured in the qemu
coredump. The qemu process that has been dumped must have had the
command line option "-machine dump-guest-core=on" which is the default.

For simplicity, the "paging", "begin" and "end" parameters of the QMP
command are not supported -- no attempt is made to get the guest's
internal paging structures (ie. paging=false is hard-wired), and guest
memory is always fully dumped.

Currently aarch64-be, aarch64-le, X86_64, 386, s390, ppc64-be,
ppc64-le guests are supported.

The CORE/NT_PRSTATUS and QEMU notes (that is, the VCPUs' statuses) are
not written to the vmcore. Preparing these would require context that is
only present in the KVM host kernel module when the guest is alive. A
fake ELF note is written instead, only to keep the ELF parser of "crash"
happy.

Dependent on how busted the qemu process was at the time of the
coredump, this command might produce unpredictable results. If qemu
deliberately called abort(), or it was dumped in response to a signal at
a halfway fortunate point, then its coredump should be in reasonable
shape and this command should mostly work."""

    def __init__(self):
        super(DumpGuestMemory, self).__init__("dump-guest-memory",
                                              gdb.COMMAND_DATA,
                                              gdb.COMPLETE_FILENAME)
        self.elf = None
        self.guest_phys_blocks = None

    def dump_init(self, vmcore):
        """Prepares and writes ELF structures to core file."""

        # Needed to make crash happy, data for more useful notes is
        # not available in a qemu core.
        self.elf.add_note("NONE", "EMPTY", 0)

        # We should never reach PN_XNUM for paging=false dumps,
        # there's just a handful of discontiguous ranges after
        # merging.
        # The constant is needed to account for the PT_NOTE segment.
        phdr_num = len(self.guest_phys_blocks) + 1
        assert phdr_num < PN_XNUM

        for block in self.guest_phys_blocks:
            block_size = block["target_end"] - block["target_start"]
            self.elf.add_segment(PT_LOAD, block["target_start"], block_size)

        self.elf.to_file(vmcore)

    def dump_iterate(self, vmcore):
        """Writes guest core to file."""

        qemu_core = gdb.inferiors()[0]
        for block in self.guest_phys_blocks:
            cur = block["host_addr"]
            left = block["target_end"] - block["target_start"]
            print("dumping range at %016x for length %016x" %
                  (cur.cast(UINTPTR_T), left))

            while left > 0:
                chunk_size = min(TARGET_PAGE_SIZE, left)
                chunk = qemu_core.read_memory(cur, chunk_size)
                vmcore.write(chunk)
                cur += chunk_size
                left -= chunk_size

    def phys_memory_read(self, addr, size):
        qemu_core = gdb.inferiors()[0]
        for block in self.guest_phys_blocks:
            if block["target_start"] <= addr \
               and addr + size <= block["target_end"]:
                haddr = block["host_addr"] + (addr - block["target_start"])
                return qemu_core.read_memory(haddr, size)
        return None

    def add_vmcoreinfo(self):
        if gdb.lookup_symbol("vmcoreinfo_realize")[0] is None:
            return
        vmci = 'vmcoreinfo_realize::vmcoreinfo_state'
        if not gdb.parse_and_eval("%s" % vmci) \
           or not gdb.parse_and_eval("(%s)->has_vmcoreinfo" % vmci):
            return

        fmt = gdb.parse_and_eval("(%s)->vmcoreinfo.guest_format" % vmci)
        addr = gdb.parse_and_eval("(%s)->vmcoreinfo.paddr" % vmci)
        size = gdb.parse_and_eval("(%s)->vmcoreinfo.size" % vmci)

        fmt = le16_to_cpu(fmt)
        addr = le64_to_cpu(addr)
        size = le32_to_cpu(size)

        if fmt != VMCOREINFO_FORMAT_ELF:
            return

        vmcoreinfo = self.phys_memory_read(addr, size)
        if vmcoreinfo:
            self.elf.add_vmcoreinfo_note(bytes(vmcoreinfo))

    def invoke(self, args, from_tty):
        """Handles command invocation from gdb."""

        # Unwittingly pressing the Enter key after the command should
        # not dump the same multi-gig coredump to the same file.
        self.dont_repeat()

        argv = gdb.string_to_argv(args)
        if len(argv) != 2:
            raise gdb.GdbError("usage: dump-guest-memory FILE ARCH")

        self.elf = ELF(argv[1])
        self.guest_phys_blocks = get_guest_phys_blocks()
        self.add_vmcoreinfo()

        with open(argv[0], "wb") as vmcore:
            self.dump_init(vmcore)
            self.dump_iterate(vmcore)

DumpGuestMemory()
