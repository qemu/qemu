# This python script adds a new gdb command, "dump-guest-memory". It
# should be loaded with "source dump-guest-memory.py" at the (gdb)
# prompt.
#
# Copyright (C) 2013, Red Hat, Inc.
#
# Authors:
#   Laszlo Ersek <lersek@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later. See
# the COPYING file in the top-level directory.
#
# The leading docstring doesn't have idiomatic Python formatting. It is
# printed by gdb's "help" command (the first line is printed in the
# "help data" summary), and it should match how other help texts look in
# gdb.

import struct

class DumpGuestMemory(gdb.Command):
    """Extract guest vmcore from qemu process coredump.

The sole argument is FILE, identifying the target file to write the
guest vmcore to.

This GDB command reimplements the dump-guest-memory QMP command in
python, using the representation of guest memory as captured in the qemu
coredump. The qemu process that has been dumped must have had the
command line option "-machine dump-guest-core=on".

For simplicity, the "paging", "begin" and "end" parameters of the QMP
command are not supported -- no attempt is made to get the guest's
internal paging structures (ie. paging=false is hard-wired), and guest
memory is always fully dumped.

Only x86_64 guests are supported.

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

    TARGET_PAGE_SIZE = 0x1000
    TARGET_PAGE_MASK = 0xFFFFFFFFFFFFF000

    # Various ELF constants
    EM_X86_64   = 62        # AMD x86-64 target machine
    ELFDATA2LSB = 1         # little endian
    ELFCLASS64  = 2
    ELFMAG      = "\x7FELF"
    EV_CURRENT  = 1
    ET_CORE     = 4
    PT_LOAD     = 1
    PT_NOTE     = 4

    # Special value for e_phnum. This indicates that the real number of
    # program headers is too large to fit into e_phnum. Instead the real
    # value is in the field sh_info of section 0.
    PN_XNUM = 0xFFFF

    # Format strings for packing and header size calculation.
    ELF64_EHDR = ("4s" # e_ident/magic
                  "B"  # e_ident/class
                  "B"  # e_ident/data
                  "B"  # e_ident/version
                  "B"  # e_ident/osabi
                  "8s" # e_ident/pad
                  "H"  # e_type
                  "H"  # e_machine
                  "I"  # e_version
                  "Q"  # e_entry
                  "Q"  # e_phoff
                  "Q"  # e_shoff
                  "I"  # e_flags
                  "H"  # e_ehsize
                  "H"  # e_phentsize
                  "H"  # e_phnum
                  "H"  # e_shentsize
                  "H"  # e_shnum
                  "H"  # e_shstrndx
                 )
    ELF64_PHDR = ("I"  # p_type
                  "I"  # p_flags
                  "Q"  # p_offset
                  "Q"  # p_vaddr
                  "Q"  # p_paddr
                  "Q"  # p_filesz
                  "Q"  # p_memsz
                  "Q"  # p_align
                 )

    def __init__(self):
        super(DumpGuestMemory, self).__init__("dump-guest-memory",
                                              gdb.COMMAND_DATA,
                                              gdb.COMPLETE_FILENAME)
        self.uintptr_t     = gdb.lookup_type("uintptr_t")
        self.elf64_ehdr_le = struct.Struct("<%s" % self.ELF64_EHDR)
        self.elf64_phdr_le = struct.Struct("<%s" % self.ELF64_PHDR)

    def int128_get64(self, val):
        assert (val["hi"] == 0)
        return val["lo"]

    def qtailq_foreach(self, head, field_str):
        var_p = head["tqh_first"]
        while (var_p != 0):
            var = var_p.dereference()
            yield var
            var_p = var[field_str]["tqe_next"]

    def qemu_get_ram_block(self, ram_addr):
        ram_blocks = gdb.parse_and_eval("ram_list.blocks")
        for block in self.qtailq_foreach(ram_blocks, "next"):
            if (ram_addr - block["offset"] < block["length"]):
                return block
        raise gdb.GdbError("Bad ram offset %x" % ram_addr)

    def qemu_get_ram_ptr(self, ram_addr):
        block = self.qemu_get_ram_block(ram_addr)
        return block["host"] + (ram_addr - block["offset"])

    def memory_region_get_ram_ptr(self, mr):
        if (mr["alias"] != 0):
            return (self.memory_region_get_ram_ptr(mr["alias"].dereference()) +
                    mr["alias_offset"])
        return self.qemu_get_ram_ptr(mr["ram_addr"] & self.TARGET_PAGE_MASK)

    def guest_phys_blocks_init(self):
        self.guest_phys_blocks = []

    def guest_phys_blocks_append(self):
        print "guest RAM blocks:"
        print ("target_start     target_end       host_addr        message "
               "count")
        print ("---------------- ---------------- ---------------- ------- "
               "-----")

        current_map_p = gdb.parse_and_eval("address_space_memory.current_map")
        current_map = current_map_p.dereference()
        for cur in range(current_map["nr"]):
            flat_range   = (current_map["ranges"] + cur).dereference()
            mr           = flat_range["mr"].dereference()

            # we only care about RAM
            if (not mr["ram"]):
                continue

            section_size = self.int128_get64(flat_range["addr"]["size"])
            target_start = self.int128_get64(flat_range["addr"]["start"])
            target_end   = target_start + section_size
            host_addr    = (self.memory_region_get_ram_ptr(mr) +
                            flat_range["offset_in_region"])
            predecessor = None

            # find continuity in guest physical address space
            if (len(self.guest_phys_blocks) > 0):
                predecessor = self.guest_phys_blocks[-1]
                predecessor_size = (predecessor["target_end"] -
                                    predecessor["target_start"])

                # the memory API guarantees monotonically increasing
                # traversal
                assert (predecessor["target_end"] <= target_start)

                # we want continuity in both guest-physical and
                # host-virtual memory
                if (predecessor["target_end"] < target_start or
                    predecessor["host_addr"] + predecessor_size != host_addr):
                    predecessor = None

            if (predecessor is None):
                # isolated mapping, add it to the list
                self.guest_phys_blocks.append({"target_start": target_start,
                                               "target_end"  : target_end,
                                               "host_addr"   : host_addr})
                message = "added"
            else:
                # expand predecessor until @target_end; predecessor's
                # start doesn't change
                predecessor["target_end"] = target_end
                message = "joined"

            print ("%016x %016x %016x %-7s %5u" %
                   (target_start, target_end, host_addr.cast(self.uintptr_t),
                    message, len(self.guest_phys_blocks)))

    def cpu_get_dump_info(self):
        # We can't synchronize the registers with KVM post-mortem, and
        # the bits in (first_x86_cpu->env.hflags) seem to be stale; they
        # may not reflect long mode for example. Hence just assume the
        # most common values. This also means that instruction pointer
        # etc. will be bogus in the dump, but at least the RAM contents
        # should be valid.
        self.dump_info = {"d_machine": self.EM_X86_64,
                          "d_endian" : self.ELFDATA2LSB,
                          "d_class"  : self.ELFCLASS64}

    def encode_elf64_ehdr_le(self):
        return self.elf64_ehdr_le.pack(
                                 self.ELFMAG,                 # e_ident/magic
                                 self.dump_info["d_class"],   # e_ident/class
                                 self.dump_info["d_endian"],  # e_ident/data
                                 self.EV_CURRENT,             # e_ident/version
                                 0,                           # e_ident/osabi
                                 "",                          # e_ident/pad
                                 self.ET_CORE,                # e_type
                                 self.dump_info["d_machine"], # e_machine
                                 self.EV_CURRENT,             # e_version
                                 0,                           # e_entry
                                 self.elf64_ehdr_le.size,     # e_phoff
                                 0,                           # e_shoff
                                 0,                           # e_flags
                                 self.elf64_ehdr_le.size,     # e_ehsize
                                 self.elf64_phdr_le.size,     # e_phentsize
                                 self.phdr_num,               # e_phnum
                                 0,                           # e_shentsize
                                 0,                           # e_shnum
                                 0                            # e_shstrndx
                                )

    def encode_elf64_note_le(self):
        return self.elf64_phdr_le.pack(self.PT_NOTE,         # p_type
                                       0,                    # p_flags
                                       (self.memory_offset -
                                        len(self.note)),     # p_offset
                                       0,                    # p_vaddr
                                       0,                    # p_paddr
                                       len(self.note),       # p_filesz
                                       len(self.note),       # p_memsz
                                       0                     # p_align
                                      )

    def encode_elf64_load_le(self, offset, start_hwaddr, range_size):
        return self.elf64_phdr_le.pack(self.PT_LOAD, # p_type
                                       0,            # p_flags
                                       offset,       # p_offset
                                       0,            # p_vaddr
                                       start_hwaddr, # p_paddr
                                       range_size,   # p_filesz
                                       range_size,   # p_memsz
                                       0             # p_align
                                      )

    def note_init(self, name, desc, type):
        # name must include a trailing NUL
        namesz = (len(name) + 1 + 3) / 4 * 4
        descsz = (len(desc)     + 3) / 4 * 4
        fmt = ("<"   # little endian
               "I"   # n_namesz
               "I"   # n_descsz
               "I"   # n_type
               "%us" # name
               "%us" # desc
               % (namesz, descsz))
        self.note = struct.pack(fmt,
                                len(name) + 1, len(desc), type, name, desc)

    def dump_init(self):
        self.guest_phys_blocks_init()
        self.guest_phys_blocks_append()
        self.cpu_get_dump_info()
        # we have no way to retrieve the VCPU status from KVM
        # post-mortem
        self.note_init("NONE", "EMPTY", 0)

        # Account for PT_NOTE.
        self.phdr_num = 1

        # We should never reach PN_XNUM for paging=false dumps: there's
        # just a handful of discontiguous ranges after merging.
        self.phdr_num += len(self.guest_phys_blocks)
        assert (self.phdr_num < self.PN_XNUM)

        # Calculate the ELF file offset where the memory dump commences:
        #
        #   ELF header
        #   PT_NOTE
        #   PT_LOAD: 1
        #   PT_LOAD: 2
        #   ...
        #   PT_LOAD: len(self.guest_phys_blocks)
        #   ELF note
        #   memory dump
        self.memory_offset = (self.elf64_ehdr_le.size +
                              self.elf64_phdr_le.size * self.phdr_num +
                              len(self.note))

    def dump_begin(self, vmcore):
        vmcore.write(self.encode_elf64_ehdr_le())
        vmcore.write(self.encode_elf64_note_le())
        running = self.memory_offset
        for block in self.guest_phys_blocks:
            range_size = block["target_end"] - block["target_start"]
            vmcore.write(self.encode_elf64_load_le(running,
                                                   block["target_start"],
                                                   range_size))
            running += range_size
        vmcore.write(self.note)

    def dump_iterate(self, vmcore):
        qemu_core = gdb.inferiors()[0]
        for block in self.guest_phys_blocks:
            cur  = block["host_addr"]
            left = block["target_end"] - block["target_start"]
            print ("dumping range at %016x for length %016x" %
                   (cur.cast(self.uintptr_t), left))
            while (left > 0):
                chunk_size = min(self.TARGET_PAGE_SIZE, left)
                chunk = qemu_core.read_memory(cur, chunk_size)
                vmcore.write(chunk)
                cur  += chunk_size
                left -= chunk_size

    def create_vmcore(self, filename):
        vmcore = open(filename, "wb")
        self.dump_begin(vmcore)
        self.dump_iterate(vmcore)
        vmcore.close()

    def invoke(self, args, from_tty):
        # Unwittingly pressing the Enter key after the command should
        # not dump the same multi-gig coredump to the same file.
        self.dont_repeat()

        argv = gdb.string_to_argv(args)
        if (len(argv) != 1):
            raise gdb.GdbError("usage: dump-guest-memory FILE")

        self.dump_init()
        self.create_vmcore(argv[0])

DumpGuestMemory()
