# TODO: rename to cosi
COSI = 'cosi'

from dataclasses import dataclass

class VolatilitySymbol:
    '''
    A reference to an entry in the volatility symbol table
    '''

    def __init__(self, panda, raw_ptr):
        self.panda = panda
        self.inner = raw_ptr

    def addr(self) -> int:
        '''
        Get the address of the symbol in memory, accounting for KASLR
        '''

        return self.panda.plugins[COSI].addr_of_symbol(self.inner)

    def value(self) -> int:
        '''
        Get the raw value for the symbol from the volatility symbol table
        '''

        return self.panda.plugins[COSI].value_of_symbol(self.inner)

    def name(self) -> str:
        '''
        Get the name for the given symbol
        '''

        name_ptr = self.panda.plugins[COSI].name_of_symbol(self.inner)
        name = self.panda.ffi.string(name_ptr).decode('utf8')
        self.panda.plugins[COSI].free_cosi_str(name_ptr)

        return name

@dataclass
class VolatilityStructField:
    '''
    A single field in a volatility struct
    '''

    name: str
    offset: int
    type_name: str

class VolatilityStruct:
    '''
    A reference to a struct in the volatility symbol table
    '''

    def __init__(self, panda, raw_ptr):
        self.panda = panda
        self.inner = raw_ptr

    def __getitem__(self, item):
        if type(item) is int:
            name = self.get_field_by_index(item)
            if name:
                offset = self.offset_of_field(name)
                type_name = self.type_of_field(name)

                return VolatilityStructField(
                    name=name,
                    offset=offset,
                    type_name=type_name
                )
            else:
                raise IndexError("Index {item} is out of range of the length of the struct fields")
        elif type(item) is str:
            name = item
            offset = self.offset_of_field(name)
            type_name = self.type_of_field(name)

            return VolatilityStructField(
                name=name,
                offset=offset,
                type_name=type_name
            )
        else:
            raise Exception(f"Invalid type {type(item)} for indexing VolatilityStruct")

    def get_field_by_index(self, index: int) -> str:
        '''
        Return the name of the field at a given index, returning `None` past the end
        of the fields.
        '''

        field_name = self.panda.plugins[COSI].get_field_by_index(self.inner, index)

        if field_name == self.panda.ffi.NULL:
            return None
        else:
            return self.panda.ffi.string(field_name).decode('utf8')

    def name(self) -> str:
        '''
        Get the name of the given struct
        '''

        name_ptr = self.panda.plugins[COSI].name_of_struct(self.inner)
        name = self.panda.ffi.string(name_ptr).decode('utf8')
        self.panda.plugins[COSI].free_cosi_str(name_ptr)

        return name

    def offset_of_field(self, name: str) -> int:
        '''
        Get the offset of a given field from the field name
        '''

        name = name.encode('utf8')
        name = self.panda.ffi.new("char[]", name)
        return self.panda.plugins[COSI].offset_of_field(self.inner, name)

    def type_of_field(self, name: str) -> str:
        '''
        Get the type of a given field from the field name
        '''

        name = name.encode('utf8')
        name = self.panda.ffi.new("char[]", name)
        type_name = self.panda.plugins[COSI].type_of_field(self.inner, name)
        type_name = self.panda.ffi.string(type_name).decode('utf8')

        return type_name

    def size(self) -> int:
        '''
        Get the total size of the given struct in bytes
        '''

        return self.panda.plugins[COSI].size_of_struct(self.inner)

    def fields(self):
        '''
        Iterate over the fields of the structure, yielding tuples in the form of
        (offset, type, field_name)
        '''
        i = 0

        while True:
            field = self.get_field_by_index(i)

            if not field:
                break

            name = field
            offset = self.offset_of_field(field)
            type_name = self.type_of_field(field)

            yield (offset, type_name, name)

            i += 1

    def at(self, ptr):
        '''
        Get a CosiGuestPointer of this type
        '''

        type_name = self.name()
        return CosiGuestPointer(self.panda, type_name, ptr)

class VolatilityBaseType:
    '''
    A reference to a base type in the volatility symbol table
    '''

    def __init__(self, panda, raw_ptr):
        self.panda = panda
        self.inner = raw_ptr

    def name(self) -> str:
        '''
        Get the name for the given base type
        '''

        name_ptr = self.panda.plugins[COSI].name_of_base_type(self.inner)
        name = self.panda.ffi.string(name_ptr).decode('utf8')
        self.panda.plugins[COSI].free_cosi_str(name_ptr)

        return name

    def size(self) -> int:
        '''
        Get the size of the given base type in bytes
        '''

        return self.panda.plugins[COSI].size_of_base_type(self.inner)

    def is_signed(self) -> bool:
        '''
        Get whether an integer base type is signed or not
        '''

        return self.panda.plugins[COSI].is_base_type_signed(self.inner)

class Cosi:
    '''
    Object to interact with the `cosi` PANDA plugin. An instance can be foudn at
    `panda.cosi`, where `panda` is a `Panda` object.
    '''

    def __init__(self, panda):
        self.panda = panda

    def symbol_addr_from_name(self, name: str) -> int:
        '''
        Given a symbol `name`, return the address in memory where it is located,
        accounting for KASLR as needed.
        '''

        name = name.encode('utf8')
        name = self.panda.ffi.new("char[]", name)
        addr = self.panda.plugins[COSI].symbol_addr_from_name(name)
        return addr

    def symbol_value_from_name(self, name: str) -> int:
        '''
        Given a symbol `name`, return the corresponding value in the volatility symbol
        table, not accounting for KASLR.
        '''

        name = name.encode('utf8')
        name = self.panda.ffi.new("char[]", name)
        addr = self.panda.plugins[COSI].symbol_value_from_name(name)
        return addr

    def kaslr_offset(self):
        '''
        Get the KASLR offset for the given system
        '''

        cpu = self.panda.get_cpu()
        offset = self.panda.plugins[COSI].kaslr_offset(cpu)

        return offset

    def symbol_from_name(self, name: str) -> VolatilitySymbol:
        '''
        Get a reference to a given symbol given the name of the symbol
        '''

        name = name.encode('utf8')
        name = self.panda.ffi.new("char[]", name)
        symbol = self.panda.plugins[COSI].symbol_from_name(name)

        return VolatilitySymbol(self.panda, symbol)

    def base_type_from_name(self, name: str) -> VolatilityBaseType:
        '''
        Get a reference to a given base type from the volatility symbol table
        '''

        name = name.encode('utf8')
        name = self.panda.ffi.new("char[]", name)
        base_type = self.panda.plugins[COSI].base_type_from_name(name)

        if base_type == self.panda.ffi.NULL:
            return None

        return VolatilityBaseType(self.panda, base_type)

    def type_from_name(self, name: str) -> VolatilityStruct:
        '''
        Get a reference to a given struct from the volatility symbol table
        '''

        name = name.encode('utf8')
        name = self.panda.ffi.new("char[]", name)
        struct = self.panda.plugins[COSI].type_from_name(name)

        if struct == self.panda.ffi.NULL:
            return None

        return VolatilityStruct(self.panda, struct)

    def per_cpu_offset(self) -> int:
        '''
        Gets the offset for per cpu variable pointers
        '''

        cpu = self.panda.get_cpu()
        return self.panda.plugins[COSI].current_cpu_offset(cpu)

    def find_per_cpu_address(self, symbol: str) -> int:
        '''
        Get the address for a symbol given that it is a per-cpu variable
        '''
        panda = self.panda

        symbol_offset = self.symbol_value_from_name(symbol)
        ptr_to_ptr = self.per_cpu_offset() + symbol_offset

        ptr_size = panda.bits // 8

        ptr = panda.virtual_memory_read(panda.get_cpu(), ptr_to_ptr, ptr_size, fmt='int') & ((1 << panda.bits) - 1)

        return ptr

    def get(self, global_type, symbol, per_cpu=False):
        if per_cpu:
            addr = self.find_per_cpu_address(symbol)
        else:
            addr = self.symbol_addr_from_name(symbol)

        return self.panda.cosi.type_from_name(global_type).at(addr)

    # ============= Task API ============= 

    def current_process(self):
        '''
        Get info about the current process
        '''

        proc = self.panda.plugins[COSI].get_current_cosiproc(self.panda.get_cpu())

        if proc == self.panda.ffi.NULL:
            return None

        return CosiProcess(self.panda, proc)

    def process_list(self):
        '''
        Get a list of the current processes
        '''

        proc_list = self.panda.plugins[COSI].cosi_get_proc_list(self.panda.get_cpu())

        if proc_list == self.panda.ffi.NULL:
            return []
        else:
            return CosiProcList(self.panda, proc_list)

    def current_thread(self):
        '''
        Get info about the current thread
        '''

        thread = self.panda.plugins[COSI].get_current_cosithread(self.panda.get_cpu())

        if thread == self.panda.ffi.NULL:
            return None

        return CosiThread(self.panda, thread)

    def current_files(self):
        '''
        Get information about the files open in the current process
        '''

        files = self.panda.plugins[COSI].get_current_files(self.panda.get_cpu())

        if files == self.panda.ffi.NULL:
            return []

        return CosiFiles(self.panda, files)

class CosiFiles:
    def __init__(self, panda, files):
        self.inner = files
        self.panda = panda

    def __del__(self):
        self.panda.plugins[COSI].free_cosi_files(self.inner)
        self.inner = None

    def __len__(self) -> int:
        return self.panda.plugins[COSI].cosi_files_len(self.inner)

    def __getitem__(self, key: int):
        if not type(key) is int:
            raise TypeError("CosiFiles must be indexed with an integer")

        file_ptr = self.panda.plugins[COSI].cosi_files_get(self.inner, key)

        if file_ptr == self.panda.ffi.NULL:
            raise IndexError("Integer {} out of bounds of CosiFiles len")

        return CosiFile(self.panda, file_ptr, hold_ref=self)

    def __iter__(self):
        for i in range(len(self)):
            yield self[i]

    def get_from_fd(self, fd: int):
        '''
        Gets a CosiFile from this set of files based on the file descriptor.

        Returns None if the fd could not be found.
        '''

        file_ptr = self.panda.plugins['COSI'].cosi_files_file_from_fd(self.inner, fd)

        if file_ptr == self.panda.ffi.NULL:
            return None
        else:
            return CosiFile(self.panda, file_ptr)

class CosiFile:
    def __init__(self, panda, file_ptr, hold_ref=None):
        self.inner = file_ptr
        self.panda = panda
        self.hold_ref = hold_ref

    def get_name(self) -> str:
        '''
        Get the name/path from which this file was accessed
        '''

        cstr_name = self.panda.plugins[COSI].cosi_file_name(self.inner)
        name = self.panda.ffi.string(cstr_name)
        self.panda.plugins[COSI].free_cosi_str(cstr_name)
        return name.decode('utf8')

    def __getattr__(self, key):
        if key == "name":
            return self.get_name()

        attr = getattr(self.inner, key, None)
        if not attr is None:
            return attr

        return getattr(self.inner.file_struct, key)

class CosiThread:
    def __init__(self, panda, thread):
        self.inner = thread
        self.panda = panda

    def __del__(self):
        self.panda.plugins[COSI].free_thread(self.inner)
        self.inner = None

    def __getattr__(self, key):
        return getattr(self.inner, key)

class CosiProcess:
    def __init__(self, panda, proc, hold_ref=None):
        self.panda = panda
        self.inner = proc
        self.hold_ref = hold_ref

    def __del__(self):
        if self.hold_ref is None:
            self.panda.plugins[COSI].free_process(self.inner)
        self.inner = None

    def get_name(self):
        cstr_name = self.panda.plugins[COSI].cosi_proc_name(self.inner)
        name = self.panda.ffi.string(cstr_name)
        self.panda.plugins[COSI].free_cosi_str(cstr_name)
        return name.decode('utf8')

    def __getattr__(self, key):
        if key == "name":
            return self.get_name()

        attr = getattr(self.inner, key, None)
        if not attr is None:
            return attr

        return getattr(self.inner.task, key)

    def open_files(self):
        '''
        Returns information about all the files open in this process
        '''

        files = self.panda.plugins[COSI].cosi_proc_files(self.inner)
        return CosiFiles(self.panda, files)

    def children(self):
        '''
        Returns a list of this process' children
        '''

        children = self.panda.plugins[COSI].cosi_proc_children(
            self.panda.get_cpu(),
            self.inner
        )

        if children == self.panda.ffi.NULL:
            return []

        return CosiProcList(self.panda, children)

    def mappings(self):
        '''
        Returns a list of the mappings of the process
        '''

        mappings = self.panda.plugins[COSI].cosi_proc_get_mappings(
            self.panda.get_cpu(),
            self.inner
        )

        if mappings == self.panda.ffi.NULL:
            return []

        return CosiMappings(self.panda, mappings)

class CosiProcList:
    def __init__(self, panda, inner):
        self.inner = inner
        self.panda = panda

    def __del__(self):
        self.panda.plugins[COSI].cosi_free_proc_list(self.inner)
        self.inner = None

    def __len__(self) -> int:
        return self.panda.plugins[COSI].cosi_proc_list_len(self.inner)

    def __getitem__(self, key: int):
        if not type(key) is int:
            raise TypeError("CosiProcList must be indexed with an integer")

        file_ptr = self.panda.plugins[COSI].cosi_proc_list_get(self.inner, key)

        if file_ptr == self.panda.ffi.NULL:
            raise IndexError("Integer {} out of bounds of CosiProcList length")

        return CosiProcess(self.panda, file_ptr, hold_ref=self)

    def __iter__(self):
        for i in range(len(self)):
            yield self[i]

class CosiModule:
    def __init__(self, panda, proc, hold_ref=None):
        self.panda = panda
        self.inner = proc
        self.hold_ref = hold_ref

    def __del__(self):
        if self.hold_ref is None:
            pass # TODO: free module if it's not a reference
        self.inner = None

    def get_name(self):
        cstr_name = self.panda.plugins[COSI].cosi_module_name(self.inner)
        name = self.panda.ffi.string(cstr_name)
        self.panda.plugins[COSI].free_cosi_str(cstr_name)
        return name.decode('utf8')

    def get_file(self):
        cstr_name = self.panda.plugins[COSI].cosi_module_file(self.inner)
        name = self.panda.ffi.string(cstr_name)
        self.panda.plugins[COSI].free_cosi_str(cstr_name)
        return name.decode('utf8')

    def __getattr__(self, key):
        if key == "name":
            return self.get_name()
        elif key == "file":
            return self.get_file()

        attr = getattr(self.inner, key, None)
        if not attr is None:
            return attr

        return getattr(self.inner.task, key)

class CosiMappings:
    def __init__(self, panda, inner):
        self.inner = inner
        self.panda = panda

    def __del__(self):
        self.panda.plugins[COSI].cosi_free_mappings(self.inner)
        self.inner = None

    def __len__(self) -> int:
        return self.panda.plugins[COSI].cosi_mappings_len(self.inner)

    def __getitem__(self, key: int):
        if not type(key) is int:
            raise TypeError("CosiMappings must be indexed with an integer")

        file_ptr = self.panda.plugins[COSI].cosi_mappings_get(self.inner, key)

        if file_ptr == self.panda.ffi.NULL:
            raise IndexError("Integer {} out of bounds of CosiProcList length")

        return CosiModule(self.panda, file_ptr, hold_ref=self)

    def __iter__(self):
        for i in range(len(self)):
            yield self[i]

import struct

struct_types = {
    'float': ('f', 4),
    'double': ('d', 8),
    'char': ('c', 1),
    '_Bool': ('?', 1),
    'short': ('h', 2),
    'unsigned short': ('H', 2),
    'int': ('i', 4),
    'unsigned int': ('I', 4),
    'long': ('l', 4),
    'unsigned long': ('L', 4),
    'long long': ('q', 8),
    'unsigned long long': ('Q', 8),
}

types_from_size = {
    1: 'b',
    2: 'h',
    4: 'i',
    8: 'q'
}

class CosiIntrusiveListAccessor:
    def __init__(self, panda, ptr):
        self._panda = panda
        self._ptr = ptr

    def __getattr__(self, name):
        list_head = getattr(self._ptr, name)
        if list_head._type_name != 'list_head':
            raise Exception(f"Field {self._ptr._type_name}.{name} not a list_head*")

        field_offset = self._ptr._type[name].offset

        ptrs = []
        head = list_head.next.prev
        current = list_head.next
        while head._ptr != current._ptr:
            addr = current._ptr - field_offset
            ptrs.append(CosiGuestPointer(self._panda, self._ptr._type_name, addr))
            current = current.next

        return ptrs

class CosiGuestPointer:
    '''
    A type representing a pointer for a data structure in the kernel
    '''

    def __init__(self, panda, type_name, ptr, parent=None):
        self._panda = panda
        self._type_name = type_name
        self._ptr = ptr
        self._type = panda.cosi.type_from_name(type_name)
        self._parent = parent

    def __repr__(self):
        return f'<CosiGuestPointer of type {self._type_name} at {hex(self._ptr)}>'

    def _read_type(self, field_ptr: int, field_type: str, fallback=False):
        panda = self._panda
        cpu = panda.get_cpu()

        # pointer
        if field_type[-1] == '*':
            pointer = panda.virtual_memory_read(cpu, field_ptr, panda.bits // 8, fmt='int')

            inner_type = field_type[:-1]
            if inner_type.startswith('struct '):
                inner_type = inner_type[len('struct '):]
            return CosiGuestPointer(panda, inner_type, pointer, parent=self)
        # inline struct
        elif field_type.startswith('struct '):
            field_type_inner = field_type[len('struct '):]

            return CosiGuestPointer(panda, field_type_inner, field_ptr, parent=self)
        # char array
        elif field_type.startswith('char[') and field_type[-1] == ']':
            length = int(field_type[len('char['):-1])
            data = panda.virtual_memory_read(cpu, field_ptr, length)

            try:
                length = data.index(b'\0')
                data = data[:length]
            except ValueError:
                pass

            return data.decode('utf8')

        # non-char array
        elif field_type[-1] == ']':
            return CosiGuestPointer(panda, field_type, field_ptr, parent=self)

        # basic type
        elif field_type in struct_types:
            specifier, size = struct_types[field_type]

            val_bytes = panda.virtual_memory_read(cpu, field_ptr, size)
            return struct.unpack(
                ('<' if panda.endianness == 'little' else '>') + specifier, val_bytes
            )[0]
        # platform-specific base types
        elif base_type := panda.cosi.base_type_from_name(field_type):
            size = base_type.size()
            is_signed = base_type.is_signed()

            specifier = types_from_size[size]
            if not is_signed:
                specifier = specifier.upper()

            val_bytes = panda.virtual_memory_read(cpu, field_ptr, size)

            return struct.unpack(
                ('<' if panda.endianness == 'little' else '>') + specifier, val_bytes
            )[0]

        elif fallback:
            inner_type = field_type
            if inner_type.startswith('struct '):
                inner_type = inner_type[len('struct '):]
            return CosiGuestPointer(panda, inner_type, field_ptr, parent=self)
        else:
            raise Exception(f"Dereferencing type {field_type} is unsupported")

    def _type_len(self, inner_type):
        panda = self._panda

        # array of pointer
        if inner_type[-1] == '*':
            inner_type_size = panda.bits // 8

        # array of primitive
        elif inner_type in struct_types:
            inner_type_size = struct_types[inner_type][1]

        # array of platform-specific base types
        elif base_type := panda.cosi.base_type_from_name(inner_type):
            inner_type_size = base_type.size()

        # array of structs
        elif struct_type := panda.cosi.type_from_name(inner_type):
            inner_type_size = struct_type.size()

        else:
            raise ValueError(f'sizeof({inner_type}) cannot be determined')

        return inner_type_size

    def __getattr__(self, name):
        field = self._type[name]
        field_ptr = self._ptr + field.offset
        field_type = field.type_name

        return self._read_type(field_ptr, field_type)

    def __getitem__(self, item):
        panda = self._panda

        # pointer to array
        if self._type_name[-1] == ']':
            inner_type, count = self._type_name[:-1].split('[')
            length = int(count)

            inner_type_size = self._type_len(inner_type)

            # indexing a single field
            if type(item) is int:
                index = item
                offset = inner_type_size * index

                return self._read_type(self._ptr + offset, inner_type, fallback=True)

            # slicing the array
            elif type(item) is slice:
                out = []
                for i in range(0, length)[item]:
                    offset = inner_type_size * i
                    item = self._read_type(self._ptr + offset, inner_type, fallback=True)
                    out.append(item)
                return out
            else:
                raise ValueError(f'CosiGuestPointer cannot be sliced with type of {type(item)}')

        # indexing pointer
        elif type(item) is int:
            index = item
            inner_type_size = self._type_len(self._type_name)
            offset = inner_type_size * index

            return self._read_type(self._ptr + offset, self._type_name, fallback=True)

        # slicing pointer
        elif type(item) is slice:
            if item.stop is None or item.stop < 0:
                raise ValueError("Cannot slice a pointer without an end")

            inner_type_size = self._type_len(self._type_name)

            out = []
            for i in range(item.start or 0, item.stop, item.step or 1):
                offset = inner_type_size * i
                item = self._read_type(self._ptr + offset, self._type_name, fallback=True)
                out.append(item)
            return out

        # accessing field
        else:
            return self.__getattr__(item)

    def __dir__(self):
        if self._type is None:
            return ['null_terminated', 'get_raw_ptr', 'cast']
        else:
            return [field[2] for field in self._type.fields()]

    def before(self, type_name: str):
        '''
        Returns a pointer to the data following the current pointer of type `type_name`
        '''

        size = self._panda.cosi.type_from_name(type_name).size()
        print(size)
        ptr = self._ptr - size

        return CosiGuestPointer(self._panda, type_name, ptr)

    def after(self, type_name: str):
        '''
        Returns a pointer to the data following the current pointer of type `type_name`
        '''

        ptr = self._ptr + self._type.size()

        return CosiGuestPointer(self._panda, type_name, ptr)

    def as_linux_list(self, sibling: str, list_entry_type=None) -> list:
        '''
        Takes a list_head* and reads it into a list. If no `list_entry_type` is provided,
        it is assumed to be equivelant to the parent struct the `list_head*` came from.

        For example, if current_task.children is a `list_head*`, the parent would be
        `current_task` (of type `task_struct`), so the list would default to being a list of
        `task_struct` (of which `sibling` should be passed a value of `"sibling"`).

        So if one does `current_task.children.as_linux_list("sibling")` it will return
        a list of `CosiGuestPointer`s pointing to `task_struct`s.
        '''

        if type(list_entry_type) is str:
            list_type_name = list_entry_type
            list_entry_type = self._panda.cosi.type_from_name(list_entry_type)
        elif list_entry_type is None:
            parent = self._parent
            if parent is None:
                raise ValueError("The list_head has no parent and `list_entry_type` was not provided")
            list_entry_type = parent._type
            list_type_name = parent._type_name

        field_offset = list_entry_type.offset_of_field(sibling)

        ptrs = []
        head = self.next
        current = head
        while head._ptr != current.next._ptr and current._ptr != self._ptr:
            addr = current._ptr - field_offset
            ptrs.append(CosiGuestPointer(self._panda, list_type_name, addr))
            current = current.next

        return ptrs

    def deref(self):
        return self[0]

    def null_terminated(self) -> str:
        '''
        Read a CosiGuestPointer for a `char*` as a null-terminated string
        '''

        panda = self._panda
        inner_type = self._type_name
        cpu = panda.get_cpu()

        if inner_type == 'char' or inner_type == 'unsigned char':
            return panda.read_str(cpu, self._ptr)
        else:
            raise ValueError("Cannot call read_null_terminated on {inner_type}*")

    def get_raw_ptr(self) -> int:
        '''
        Get the address in memory this points to
        '''

        return self._ptr

    def cast(self, cast_to: str):
        '''
        Cast to a pointer of another type
        '''

        return CosiGuestPointer(self._panda, cast_to, self._ptr, parent=self._parent)

    def container_of(self, type_name: str, field_name: str):
        '''
        Get a pointer to the struct containing this type
        '''

        container_type = self._panda.cosi.type_from_name(type_name)
        offset_in_container = container_type.offset_of_field(field_name)

        return container_type.at(self._ptr - offset_in_container)
