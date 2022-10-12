'''
This module contains architecture-specific code.

When the `pandare.panda` class is initialized, it will automatically
initialize a PandaArch class for the specified architecture in the variable
`panda.arch`.

'''
import binascii
import struct
from .utils import telescope

class PandaArch():
    '''
    Base class for architecture-specific implementations for PANDA-supported architectures
    '''
    def __init__(self, panda):
        '''
        Initialize a PANDA-supported architecture and hold a handle on the PANDA object
        '''
        self.panda = panda

        self.reg_sp      = None # Stack pointer register ID if stored in a register
        self.reg_pc      = None # PC register ID if stored in a register
        self.reg_retaddr = None # Register ID that contains return address
        self.reg_retval  = None # convention: register name that contains return val
        self.call_conventions = None # convention: ['reg_for_arg0', 'reg_for_arg1',...]
        self.registers = {}
        '''
        Mapping of register names to indices into the appropriate CPUState array
        '''

    def _determine_bits(self):
        '''
        Determine bits and endianness for the panda object's architecture
        '''
        bits = None
        endianness = None # String 'little' or 'big'
        if self.panda.arch_name == "i386":
            bits = 32
            endianness = "little"
        elif self.panda.arch_name == "x86_64":
            bits = 64
            endianness = "little"
        elif self.panda.arch_name == "arm":
            endianness = "little" # XXX add support for arm BE?
            bits = 32
        elif self.panda.arch_name == "aarch64":
            bits = 64
            endianness = "little" # XXX add support for arm BE?
        elif self.panda.arch_name == "ppc":
            bits = 32
            endianness = "big"
        elif self.panda.arch_name == "mips":
            bits = 32
            endianness = "big"
        elif self.panda.arch_name == "mipsel":
            bits = 32
            endianness = "little"
        elif self.panda.arch_name == "mips64":
            bits = 64
            endianness = "big"
        elif self.panda.arch_name == "mips64el":
            bits = 64
            endianness = "little"

        assert (bits is not None), f"Missing num_bits logic for {self.panda.arch_name}"
        assert (endianness is not None), f"Missing endianness logic for {self.panda.arch_name}"
        register_size = int(bits/8)
        return bits, endianness, register_size

    def get_reg(self, cpu, reg):
        '''
        Return value in a `reg` which is either a register name or index (e.g., "R0" or 0)
        '''
        if isinstance(reg, str):
            reg = reg.upper()
            if reg == 'PC':
                return self.get_pc(cpu)
            if reg not in self.registers.keys():
                raise ValueError(f"Invalid register name {reg}")
            else:
                reg = self.registers[reg]

        return self._get_reg_val(cpu, reg)

    def _get_reg_val(self, cpu, idx):
        '''
        Virtual method. Must be implemented for each architecture to return contents of register specified by idx.
        '''
        raise NotImplementedError()

    def set_reg(self, cpu, reg, val):
        '''
        Set register `reg` to a value where `reg` is either a register name or index (e.g., "R0" or 0)
        '''
        if isinstance(reg, str):
            reg = reg.upper()
            if reg not in self.registers.keys():
                raise ValueError(f"Invalid register name {reg}")
            else:
                reg = self.registers[reg]
        elif not isinstance(reg, int):
            raise ValueError(f"Can't set register {reg}")

        return self._set_reg_val(cpu, reg, val)

    def _set_reg_val(self, cpu, idx, val):
        '''
        Virtual method. Must be implemented for each architecture to return contents of register specified by idx.
        '''
        raise NotImplementedError()

    def get_pc(self, cpu):
        '''
        Returns the current program counter. Must be overloaded if self.reg_pc is None
        '''
        if self.reg_pc:
            return self.get_reg(cpu, self.reg_pc)
        else:
            raise RuntimeError(f"get_pc unsupported for {self.panda.arch_name}")

    def _get_arg_loc(self, idx, convention):
        '''
        return the name of the argument [idx] for the given arch with calling [convention]
        '''

        if self.call_conventions and convention in self.call_conventions:
            if idx < len(self.call_conventions[convention]):
                return self.call_conventions[convention][idx]
            raise NotImplementedError(f"Unsupported argument number {idx}")
        raise NotImplementedError(f"Unsupported convention {convention} for {type(self)}")

    def _get_ret_val_reg(self, cpu, convention):
        if self.reg_retval and convention in self.reg_retval:
            return self.reg_retval[convention]
        raise NotImplementedError(f"Unsupported get_retval for architecture {type(self)} {convention}")


    def set_arg(self, cpu, idx, val, convention='default'):
        '''
        Set arg [idx] to [val] for given calling convention.

        Note for syscalls we define arg[0] as syscall number and then 1-index the actual args
        '''
        argloc = self._get_arg_loc(idx, convention)

        if self._is_stack_loc(argloc):
            return self._write_stack(cpu, argloc, val)
        else:
            return self.set_reg(cpu, argloc, val)

    def get_arg(self, cpu, idx, convention='default'):
        '''
        Return arg [idx] for given calling convention. This only works right as the guest
        is calling or has called a function before register values are clobbered.

        If arg[idx] should be stack-based, name it stack_0, stack_1... this allows mixed
        conventions where some args are in registers and others are on the stack (i.e.,
        mips32 syscalls).

        When doing a stack-based read, this function may raise a ValueError if the memory
        read fails (i.e., paged out, invalid address).

        Note for syscalls we define arg[0] as syscall number and then 1-index the actual args
        '''
        
        argloc = self._get_arg_loc(idx, convention)

        if self._is_stack_loc(argloc):
            return self._read_stack(cpu, argloc)
        else:
            return self.get_reg(cpu, argloc)

    @staticmethod
    def _is_stack_loc(argloc):
        '''
        Given a name returned by self._get_arg_loc
        check if it's the name of a stack offset
        '''
        return argloc.startswith("stack_")

    def _write_stack(self, cpu, argloc, val):
        '''
        Given a name like stack_X, calculate where
        the X-th value on the stack is, then write val
        to that location

        May raise a ValueError if the memory write fails
        '''

        if isinstance(val, int):
            # Encode as word-size with endianness
            bits, endianness, reg_sz = self._determine_bits()
            val = val.to_bytes(reg_sz, byteorder=endianness)

        if not isinstance(val, bytes):
            raise ValueError("_write_stack needs an int or bytes")


        # Stack based - get stack base, calculate offset, then try to read it
        assert(self._is_stack_loc(argloc)), f"Can't get stack offset of {argloc}"

        stack_idx = int(argloc.split("stack_")[1])
        stack_base = self.get_reg(cpu, self.reg_sp)
        offset = reg_sz * (stack_idx+1)
        self.panda.virtual_memory_write(cpu, stack_base + offset, val)

    def _read_stack(self, cpu, argloc):
        '''
        Given a name like stack_X, calculate where
        the X-th value on the stack is, then read it out of
        memory and return it.

        May raise a ValueError if the memory read fails
        '''
        # Stack based - get stack base, calculate offset, then try to read it
        assert(self._is_stack_loc(argloc)), f"Can't get stack offset of {argloc}"

        stack_idx = int(argloc.split("stack_")[1])
        stack_base = self.get_reg(cpu, self.reg_sp)
        arg_sz = self.panda.bits // 8
        offset = arg_sz * (stack_idx+1)
        return self.panda.virtual_memory_read(cpu, stack_base + offset, arg_sz, fmt='int')

    def set_retval(self, cpu, val, convention='default', failure=False):
        '''
        Set return val to [val] for given calling convention. This only works
        right after a function call has returned, otherwise the register will contain
        a different value.

        If the given architecture returns failure/success in a second register (i.e., the A3
        register for mips), set that according to the failure flag.

        Note the failure argument only used by subclasses that overload this function. It's provided
        in the signature here so it can be set by a caller without regard for the guest architecture.
        '''
        reg = self._get_ret_val_reg(cpu, convention)
        return self.set_reg(cpu, reg, val)

    def get_retval(self, cpu, convention='default'):
        '''
        Set return val to [val] for given calling convention. This only works
        right after a function call has returned, otherwise the register will contain
        a different value.

        Return value from syscalls is signed
        '''
        reg = self._get_ret_val_reg(cpu, convention)
        rv = self.get_reg(cpu, reg)

        if convention == 'syscall':
            rv = self.panda.from_unsigned_guest(rv)
        return rv


    def set_pc(self, cpu, val):
        '''
        Set the program counter. Must be overloaded if self.reg_pc is None
        '''
        if self.reg_pc:
            return self.set_reg(cpu, self.reg_pc, val)
        else:
            raise RuntimeError(f"set_pc unsupported for {self.panda.arch_name}")

    def dump_regs(self, cpu):
        '''
        Print (telescoping) each register and its values
        '''
        print(f"PC: {self.get_pc(cpu):x}")

        for (regname, reg) in self.registers.items():
            val = self.get_reg(cpu, reg)
            print("{}: 0x{:x}".format(regname, val), end="\t")
            telescope(self.panda, cpu, val)

    def dump_stack(self, cpu, words=8):
        '''
        Print (telescoping) most recent `words` words on the stack (from stack pointer to stack pointer + `words`*word_size)
        '''

        base_reg_s = "SP"
        base_reg_val = self.get_reg(cpu, self.reg_sp)
        if base_reg_val == 0:
            print("[WARNING: no stack pointer]")
            return
        word_size = int(self.panda.bits/8)

        _, endianness, _ = self._determine_bits()

        for word_idx in range(words):
            try:
                val_b = self.panda.virtual_memory_read(cpu, base_reg_val+word_idx*word_size, word_size)
                val = int.from_bytes(val_b, byteorder=endianness)
                print("[{}+0x{:0>2x}] == 0x{:0<8x}]: 0x{:0<8x}".format(base_reg_s, word_idx*word_size, base_reg_val+word_idx*word_size, val), end="\t")
                telescope(self.panda, cpu, val)
            except ValueError:
                print("[{}+0x{:0>2x}] == [memory read error]".format(base_reg_s, word_idx*word_size))

    def dump_state(self, cpu):
        """
        Print registers and stack
        """
        self.dump_regs(cpu)
        self.dump_stack(cpu)

    def get_args(self, cpu, num, convention='default'):
        return [self.get_arg(cpu,i, convention) for i in range(num)]

class ArmArch(PandaArch):
    '''
    Register names and accessors for ARM
    '''
    def __init__(self, panda):
        PandaArch.__init__(self, panda)
        regnames = ["R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
                    "R8", "R9", "R10", "R11", "R12", "SP", "LR", "IP"]
        self.registers = {regnames[idx]: idx for idx in range(len(regnames)) }
        """Register array for ARM"""

        self.reg_sp      = regnames.index("SP")
        self.reg_pc      = regnames.index("IP")
        self.reg_retaddr = regnames.index("LR")

        self.reg_sp = regnames.index("SP")
        self.reg_retaddr = regnames.index("LR")
        self.call_conventions = {"arm32":         ["R0", "R1", "R2", "R3"],
                                 "syscall": ["R7", "R0", "R1", "R2", "R3", "R4", "R5"], # EABI
                                 }
        self.call_conventions['default'] = self.call_conventions['arm32']
        self.call_conventions['linux_kernel'] = self.call_conventions['arm32']

        self.reg_retval = {"default":    "R0",
                           "syscall":    "R0",
                           "linux_kernel":    "R0"}
        self.reg_pc = regnames.index("IP")

    def _get_reg_val(self, cpu, reg):
        '''
        Return an arm register
        '''
        return cpu.env_ptr.regs[reg]

    def _set_reg_val(self, cpu, reg, val):
        '''
        Set an arm register
        '''
        cpu.env_ptr.regs[reg] = val

    def get_return_value(self, cpu):
        '''
        .. Deprecated:: use get_retval
        '''
        return self.get_retval(cpu)

    def get_return_address(self, cpu):
        '''
        Looks up where ret will go
        '''
        return self.get_reg(cpu, "LR") & 0xFFFF_FFFE

class Aarch64Arch(PandaArch):
    '''
    Register names and accessors for ARM64 (Aarch64)
    '''
    def __init__(self, panda):
        PandaArch.__init__(self, panda)

        regnames = ["X0",  "X1",  "X2",  "X3",  "X4",  "X5", "X6", "X7",
                    "XR",  "X9",  "X10", "X11", "X12", "X13", "X14",
                    "X15", "IP0", "IP1", "PR", "X19", "X20", "X21",
                    "X22", "X23", "X24", "X25", "X26", "X27",
                    "X28", "FP", "LR", "SP"]

        self.reg_sp = regnames.index("SP")

        self.registers = {regnames[idx]: idx for idx in range(len(regnames)) }
        """Register array for ARM"""

        self.reg_sp = regnames.index("SP")
        self.reg_retaddr = regnames.index("LR")

        self.call_conventions = {"arm64":         ["X0", "X1", "X2", "X3", "X4", "X5", "X6", "X7"],
                                 "syscall": ["XR", "X0", "X1", "X2", "X3", "X4", "X5", "X6", "X7"]}
        self.call_conventions['default'] = self.call_conventions['arm64']
        self.call_conventions['linux_kernel'] = self.call_conventions['arm64']

        self.reg_retval = {"default":    "X0",
                           "syscall":    "X0",
                           "linux_kernel":    "X0"}
        self.arm32 = ArmArch(panda)
        
        def arm32_dec(f, name):
            def wrap(*args, **kwargs):
                # first check that we have an arg
                if len(args) > 0:
                    # double check that it's a cpustate
                    cpu = args[0]
                    if "_cffi_backend" in str(type(cpu)):
                        # check if we're in arm32 mode
                        if cpu.env_ptr.aarch64 == 0:
                            func = getattr(self.arm32, name)
                            return func(*args, **kwargs)
                return f(*args, **kwargs)
            return wrap

        for attr in dir(self):
            if callable(getattr(self, attr)) and not attr.startswith('_'):
                setattr(self, attr, arm32_dec(getattr(self, attr), attr))

    def get_pc(self, cpu):
        '''
        Overloaded function to get aarch64 program counter.
        Note the PC is not stored in a general purpose register.
        '''
        return cpu.env_ptr.pc

    def set_pc(self, cpu, val):
        '''
        Overloaded function set AArch64 program counter
        '''
        cpu.env_ptr.pc = val

    def _get_reg_val(self, cpu, reg):
        '''
        Return an aarch64 register
        '''

        if reg == 32:
            print("WARNING: unsupported get sp for aarch64")
            return 0
        else:
            return cpu.env_ptr.xregs[reg]

    def _set_reg_val(self, cpu, reg, val):
        '''
        Set an aarch64 register
        '''
        cpu.env_ptr.xregs[reg] = val

    def get_return_value(self, cpu):
        '''
        .. Deprecated:: use get_retval
        '''
        return self.get_retval(cpu)

    def get_return_address(self, cpu):
        '''
        Looks up where ret will go
        '''
        return self.get_reg(cpu, "LR")

class MipsArch(PandaArch):
    '''
    Register names and accessors for 32-bit MIPS
    '''

    # Registers are:
    '''
    Register Number	Conventional Name	Usage
    $0	        $zero	Hard-wired to 0
    $1	        $at	Reserved for pseudo-instructions
    $2 - $3	$v0, $v1	Return values from functions
    $4 - $7     $a0 - $a3	Arguments to functions - not preserved by subprograms
    $8 - $15	$t0 - $t7	Temporary data, not preserved by subprograms
    $16 - $23	$s0 - $s7	Saved registers, preserved by subprograms
    $24 - $25	$t8 - $t9	More temporary registers, not preserved by subprograms
    $26 - $27	$k0 - $k1	Reserved for kernel. Do not use.
    $28	        $gp	Global Area Pointer (base of global data segment)
    $29	        $sp	Stack Pointer
    $30	        $fp	Frame Pointer
    $31	        $ra	Return Address
    '''

    def __init__(self, panda):
        super().__init__(panda)
        regnames = ['ZERO', 'AT', 'V0', 'V1', 'A0', 'A1', 'A2', 'A3',
                    'T0', 'T1', 'T2', 'T3', 'T4', 'T5', 'T6', 'T7',
                    'S0', 'S1', 'S2', 'S3', 'S4', 'S5', 'S6', 'S7',
                    'T8', 'T9', 'K0', 'K1', 'GP', 'SP', 'FP', 'RA']

        self.reg_sp = regnames.index('SP')
        self.reg_retaddr = regnames.index('RA')
        # Default syscall/args are for mips o32
        self.call_conventions = {"mips":          ["A0", "A1", "A2", "A3"],
                "syscall": ["V0", "A0", "A1", "A2", "A3", "stack_3", "stack_4", "stack_5", "stack_6"]} # XXX: Note it's not 0-indexed for stack args, I guess the syscall pushes stuff too
        self.call_conventions['default'] = self.call_conventions['mips']
        self.call_conventions['linux_kernel'] = self.call_conventions['mips']

        self.reg_retval =  {"default":    "V0",
                            "syscall":    'V0',
                            "linux_kernel":    'V0'}


        # note names must be stored uppercase for get/set reg to work case-insensitively
        self.registers = {regnames[idx].upper(): idx for idx in range(len(regnames)) }
        self.registers['R30'] = 30

    def get_reg(self, cpu, reg):
        '''
        Overloaded function for a few mips specific registers
        '''

        if isinstance(reg, str):
            env = cpu.env_ptr
            reg = reg.upper()
            if reg == 'HI':
                return env.CP0_EntryHi
            elif reg == 'LO':
                return env.CP0_EntryLo0
            elif reg.startswith('F') and reg[1:].isnumeric():
                num = int(reg[1:])
                _, endianness, _ = self._determine_bits()
                return int.from_bytes(bytes(env.fpus[0].fpr[num]), byteorder=endianness)
            elif reg == 'FCCR':
                return env.fpus[0].fcr0
            elif reg == 'DSPCONTROL':
                return env.active_tc.DSPControl
            elif reg == 'CP0_STATUS':
                return env.CP0_Status


        return super().get_reg(cpu, reg)

    def get_pc(self, cpu):
        '''
        Overloaded function to return the MIPS current program counter
        '''
        return cpu.env_ptr.active_tc.PC

    def set_pc(self, cpu, val):
        '''
        Overloaded function set the MIPS program counter
        '''
        cpu.env_ptr.active_tc.PC = val

    def get_retval(self, cpu, convention='default'):
        '''
        Overloaded to incorporate error data from A3 register for syscalls.

        If A3 is 1 and convention is syscall, *negate* the return value.
        This matches behavior of other architecures (where -ERRNO is returned
        on error)
        '''

        flip = 1
        if convention == 'syscall' and self.get_reg(cpu, "A3") == 1:
            flip = -1

        return flip * super().get_retval(cpu)


    def _get_reg_val(self, cpu, reg):
        '''
        Return a mips register
        '''
        return cpu.env_ptr.active_tc.gpr[reg]

    def _set_reg_val(self, cpu, reg, val):
        '''
        Set a mips register
        '''
        cpu.env_ptr.active_tc.gpr[reg] = val

    def get_return_value(self, cpu, convention='default'):
        '''
        .. Deprecated:: use get_retval
        '''
        return self.get_retval(cpu)

    def get_call_return(self, cpu):
        '''
        .. Deprecated:: use get_return_address
        '''
        return self.get_return_address(cpu)

    def get_return_address(self,cpu):
        '''
        looks up where ret will go
        '''
        return self.get_reg(cpu, "RA")

    def set_retval(self, cpu, val, convention='default', failure=False):
        '''
        Overloaded function so when convention is syscall, user can control
        the A3 register (which indicates syscall success/failure) in addition
        to the syscall return value.

        When convention == 'syscall', failure = False means A3 will bet set to 0.
        Otherwise, it will be set to 1

        '''
        if convention == 'syscall':
            # Set A3 register to indicate syscall success/failure
            self.set_reg(cpu, 'a3', failure)

            # If caller is trying to indicate error by setting a negative retval
            # for a syscall, just make it positive with A3=1
            if failure and self.panda.from_unsigned_guest(val) < 0:
                val = -1 * self.panda.from_unsigned_guest(val)

        return super().set_retval(cpu, val, convention)


class Mips64Arch(MipsArch):
    '''
    Register names and accessors for MIPS64. Inherits from MipsArch for everything
    except the register name and call conventions.
    '''

    def __init__(self, panda):
        super().__init__(panda)
        regnames = ["zero", "at",   "v0",   "v1",   "a0",   "a1",   "a2",   "a3",
                    "a4",   "a5",   "a6",   "a7",   "t0",   "t1",   "t2",   "t3",
                    "s0",   "s1",   "s2",   "s3",   "s4",   "s5",   "s6",   "s7",
                    "t8",   "t9",   "k0",   "k1",   "gp",   "sp",   "s8",   "ra"]

        self.reg_sp = regnames.index('sp')
        self.reg_retaddr = regnames.index("ra")
        # Default syscall/args are for mips 64/n32 - note the registers are different than 32
        self.call_conventions = {"mips":          ["A0", "A1", "A2", "A3"], # XXX Unsure?
                                 "syscall": ["V0", "A0", "A1", "A2", "A3", "A4", "A5"]}
        self.call_conventions['default'] = self.call_conventions['mips']
        self.call_conventions['linux_kernel'] = self.call_conventions['mips']

        self.reg_retval =  {"default":    "V0",
                            "syscall":    'V0'}


        # note names must be stored uppercase for get/set reg to work case-insensitively
        self.registers = {regnames[idx].upper(): idx for idx in range(len(regnames)) }

class PowerPCArch(PandaArch): 
    '''
    Register names and accessors for ppc
    '''
    def __init__(self, panda): 
        super().__init__(panda)
        regnames = ["r0", "sp", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", 
                    "r12", "r13", "r14", "r15", "r16", "r17", "r18", "r19", "r20", "r21", "r22", 
                    "r23", "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31"]
        self.reg_sp = regnames.index('sp')
        self.registers = {regnames[idx].upper(): idx for idx in range(len(regnames)) }
        self.registers_crf = ["CR0", "CR1", "CR2", "CR3", "CR4", "CR5", "CR6", "CR7"]

    def get_pc(self, cpu):
        '''
        Overloaded function to return the ppc current program counter
        '''
        return cpu.env_ptr.nip

    def set_pc(self, cpu, val):
        '''
        Overloaded function to set the ppc program counter
        '''
        cpu.env_ptr.nip = val

    def _get_reg_val(self, cpu, reg):
        '''
        Return a ppc register
        '''
        return cpu.env_ptr.gpr[reg]

    def _set_reg_val(self, cpu, reg, val):
        '''
        Set an x86_64 register
        '''
        cpu.env_ptr.gpr[reg] = val

    def get_reg(self, cpu, reg):

        reg = reg.upper()
        env = cpu.env_ptr
        if reg == "LR":
            return env.lr
        elif reg == "CTR": 
            return env.ctr
        elif reg in self.registers_crf: 
            return env.crf[self.registers_crf.index(reg)]
        else:
            return super().get_reg(cpu, reg)


    def set_reg(self, cpu, reg, val):
        reg = reg.upper()
        env = cpu.env_ptr

        if reg == "LR": 
            env.lr = val
        elif reg == "CTR":
            env.ctr = val
        elif reg in self.registers_crf: 
            env.crf[self.registers_crf.index(reg)] = val
        else:
            super().set_reg(cpu, reg, val)



class X86_64Arch(PandaArch):
    '''
    Register names and accessors for x86_64
    '''

    def __init__(self, panda):
        super().__init__(panda)
        # The only place I could find the R_ names is in tcg/i386/tcg-target.h:50
        regnames = ['RAX', 'RCX', 'RDX', 'RBX', 'RSP', 'RBP', 'RSI', 'RDI',
                    'R8', 'R9', 'R10', 'R11', 'R12', 'R13', 'R14', 'R15']
        # XXX Note order is A C D B, because that's how qemu does it

        self.call_conventions = {'sysv':           ['RDI', 'RSI', 'RDX', 'RCX', 'R8', 'R9'],
                                 'syscall': ['RAX', 'RDI', 'RSI', 'RDX', 'R10', 'R8', 'R9']}

        self.call_conventions['default'] = self.call_conventions['sysv']
        self.call_conventions['linux_kernel'] = self.call_conventions['sysv']

        self.reg_sp = regnames.index('RSP')
        self.reg_retval = {'sysv': 'RAX',
                           'syscall': 'RAX',
                           'linux_kernel': 'RAX'}
        self.reg_retval['default'] = self.reg_retval['sysv']

        self.registers = {regnames[idx]: idx for idx in range(len(regnames)) }

        # Internal state to support some of the weird x86-64 registers
        self.reg_names_general = ['EAX', 'ECX', 'EDX', 'EBX', 'ESP', 'EBP', 'ESI', 'EDI']
        self.reg_names_short = ['AX', 'CX', 'DX', 'BX', 'SP', 'BP', 'SI', 'DI']
        self.reg_names_byte = ['AL', 'CL', 'DL', 'BL', 'AH', 'CH', 'DH', 'BH']
        self.seg_names = ['ES', 'CS', 'SS', 'DS', 'FS', 'GS']
        self.reg_names_mmr = ['LDT', 'TR', 'GDT', 'IDT']

    def _get_segment_register(self, env, seg_name):
        seg_idx = self.seg_names.index(seg_name)
        return env.segs[seg_idx].base

    def _get_general_purpose_register(self, env, reg_name, mask):
        return env.regs[self.reg_names_general.index(reg_name)] & mask

    def _set_segment_register(self, env, seg_name, value):
        seg_idx = self.seg_names.index(seg_name)
        env.segs[seg_idx].base = value

    def _set_general_purpose_register(self, env, reg_name, value, mask):
        reg_idx = self.reg_names_general.index(reg_name)
        env.regs[reg_idx] = (env.regs[reg_idx] & ~mask) | (value & mask)

    def get_pc(self, cpu):
        '''
        Overloaded function to return the x86_64 current program counter
        '''
        return cpu.env_ptr.eip

    def get_retval(self, cpu, convention='default'):
        '''
        Overloaded to support FreeBSD syscall ABI
        In that ABI, if eflags carry bit is set, an error has occured. To standardize
        pandare.arch returns across architectures/ABIs, we indicate a failure by returnning
        -ERRNO.
        '''

        error_flip = False
        if convention == 'syscall' and self.panda.get_os_family() == 'OS_FREEBSD' and \
                self.panda.libpanda.cpu_cc_compute_all(cpu.env_ptr, 1) & 1 == 1:
            error_flip = True

        return super().get_retval(cpu, convention) * (-1 if error_flip else 1)

    def set_pc(self, cpu, val):
        '''
        Overloaded function to set the x86_64 program counter
        '''
        cpu.env_ptr.eip = val

    def _get_mmr_val(self, cpu, reg): 
        reg = reg.lower()
        sc = getattr(cpu.env_ptr, reg) 
        return (sc.selector, sc.base, sc.limit, sc.flags)

    def _set_mmr_val(self, cpu, reg, val): 
        reg = reg.lower()
        selector, base, limit, flags = val 
        sc = getattr(cpu.env_ptr, reg)
        sc.selector = selector
        sc.base = base
        sc.limit = limit
        sc.flags = flags

    def _get_reg_val(self, cpu, reg):
        '''
        Return an x86_64 register
        '''
        return cpu.env_ptr.regs[reg]

    def _set_reg_val(self, cpu, reg, val):
        '''
        Set an x86_64 register
        '''
        cpu.env_ptr.regs[reg] = val

    def get_return_value(self, cpu):
        '''
        .. Deprecated:: use get_retval
        '''
        return self.get_retval(cpu)

    def get_return_address(self, cpu):
        '''
        looks up where ret will go
        '''
        esp = self.get_reg(cpu, "RSP")
        return self.panda.virtual_memory_read(cpu, esp, 8, fmt='int')

    def get_reg(self, cpu, reg):
        '''
        X86_64 has a bunch of different ways to access registers. We support
        the regular names, the 32 and 16 bit varations (e.g., EAX, AX, AL),
        segment registers, and D/W/B style accesses to R8-R15
        '''
        if isinstance(reg, int):
            # If reg is an int, it should be an offset into our register array
            return self._get_reg_val(cpu, reg)

        reg = reg.upper()
        env = cpu.env_ptr
        if reg in self.reg_names_mmr: 
            return self._get_mmr_val(cpu, reg)
        if reg in self.seg_names:
            return self._get_segment_register(env, reg)
        elif reg in ['EFLAGS', 'RFLAGS']: 
            return env.eflags
        elif reg in ['RIP', 'PC', 'EIP']:
            pc = self.get_pc(cpu) # changes reg to 'IP' and re-calls this
            if reg == 'EIP':
                pc &= 0xFFFFFFFF
            return pc
        elif reg.startswith('XMM'):
            raw_arr = env.xmm_regs[int(reg[3:].rstrip('HLQX'))]
            _, endianness, _ = self._determine_bits()

            if reg.endswith('lq'):
                value_bytes = raw_arr[0:8] # Lower 64 bits
            elif reg.endswith('hq'):
                value_bytes = raw_arr[8:16] # Higher 64 bits
            elif reg.endswith('hx'):
                value_bytes = raw_arr[4:8] # Higher 32 bits of the lower 64 bits
            else:
                value_bytes = raw_arr[0:16] # Full 128 bits
            return int.from_bytes(bytes(value_bytes), byteorder=endianness)

        elif reg.startswith('MM'):
            raise ValueError("MM registers unsupported")
        elif reg.startswith('YMM'):
            raise ValueError("YMM registers unsupported")
        elif reg.startswith('CR'):
            return env.cr[int(reg[2:])]
        elif reg.startswith('R') and any([reg.endswith(x) for x in 'DWB']) and reg.strip('RDWB').isnumeric():
            # R8-R15 can be accessed with D (double word), W (word) and B (byte)
            # to select the lowest 32-bits, the lowest 16 bits, or the lowest 8 bits.
            reg_idx = int(reg.strip('RDWB')) - 8
            reg_suffix = reg[-1]
            mask = {'D': 0xFFFFFFFF,
                    'W': 0xFFFF,
                    'B': 0xFF}[reg_suffix]
            return env.regs[reg_idx] & mask
        elif reg in self.reg_names_general:
            return self._get_general_purpose_register(env, reg, 0xFFFFFFFF)
        elif reg in self.reg_names_short:
            return env.regs[self.reg_names_short.index(reg)] & 0xFFFF
        elif reg in self.reg_names_byte:
            reg_idx = self.reg_names_byte.index(reg)
            if reg_idx > 3:
                reg_idx -= 4
                return (env.regs[reg_idx] >> 8) & 0xFF
            else:
                return env.regs[reg_idx] & 0xFF
        else:
            return super().get_reg(cpu, reg)

    def set_reg(self, cpu, reg, val):
        reg = reg.upper()
        env = cpu.env_ptr

        if reg in self.reg_names_mmr: 
            return self._set_mmr_val(cpu, reg, val)
        elif reg in self.seg_names:
            self._set_segment_register(env, reg, val)
        elif reg in ['EFLAGS', 'RFLAGS']: 
            env.eflags = val
        elif reg in ['RIP', 'PC']:
            return self.set_pc(cpu, val) # changes reg to 'IP' and re-calls this
        elif reg.startswith('XMM'):
            env.xmm_regs[int(reg[3:])] = val
            #raise NotImplementedError("XMM registers unsupported")
        elif reg.startswith('MM'):
            raise NotImplementedError("MM registers unsupported")
        elif reg.startswith('YMM'):
            raise NotImplementedError("YMM registers unsupported")
        elif reg.startswith('CR'):
            env.cr[int(reg[2:])] = val
        elif reg.startswith('R') and any([reg.endswith(x) for x in 'DWB']):
            # R8-R15 can be accessed with D (double word), W (word) and B (byte)
            # to select the lowest 32-bits, the lowest 16 bits, or the lowest 8 bits.
            reg_idx = int(reg.strip('RDWB')) - 8
            reg_suffix = reg[-1]
            mask = {'D': 0xFFFFFFFF,
                    'W': 0xFFFF,
                    'B': 0xFF}[reg_suffix]
            env.regs[reg_idx] & mask
            env.regs[reg_idx] = (env.regs[reg_idx] & ~mask) | (val & mask)
        elif reg in self.reg_names_general:
            self._set_general_purpose_register(env, reg, val, 0xFFFFFFFF)
        elif reg in self.reg_names_short:
            self._set_general_purpose_register(env, reg, val, 0xFFFF)
        elif reg in self.reg_names_byte:
            reg_idx = self.reg_names_byte.index(reg)
            mask = 0xFF << (8 * (reg_idx > 3))
            reg_idx %= 4
            self._set_general_purpose_register(env, reg, val, mask)
        else:
            super().set_reg(cpu, reg, val)


class X86Arch(X86_64Arch):
    '''
    Register names and accessors for x86
    '''

    def __init__(self, panda):
        super().__init__(panda)
        regnames = ['EAX', 'ECX', 'EDX', 'EBX', 'ESP', 'EBP', 'ESI', 'EDI']
        # XXX Note order is A C D B, because that's how qemu does it . See target/i386/cpu.h

        # Note we don't set self.call_conventions because stack-based arg get/set is
        # not yet supported
        self.reg_retval = {"default":    "EAX",
                           "syscall":    "EAX",
                           "linux_kernel":    "EAX"}

        self.call_conventions = {"cdecl": [f"stack_{x}" for x in range(20)], # 20: arbitrary but big
                                 "syscall": ["EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP"],
                                 "linux_kernel": ["EAX", "EDX", "ECX", "stack_3", "stack_4", "stack_5", "stack_6"]}
        self.call_conventions['default'] = self.call_conventions['cdecl']

        self.reg_sp = regnames.index('ESP')
        self.registers = {regnames[idx]: idx for idx in range(len(regnames)) }
