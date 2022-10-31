#! /usr/bin/env python3

# Generate test-avx.h from x86.csv

import csv
import sys
from fnmatch import fnmatch

archs = [
    "SSE", "SSE2", "SSE3", "SSSE3", "SSE4_1", "SSE4_2",
    "AES", "AVX", "AVX2", "AES+AVX", "VAES+AVX",
    "F16C", "FMA",
]

ignore = set(["FISTTP",
    "LDMXCSR", "VLDMXCSR", "STMXCSR", "VSTMXCSR"])

imask = {
    'vBLENDPD': 0xff,
    'vBLENDPS': 0x0f,
    'CMP[PS][SD]': 0x07,
    'VCMP[PS][SD]': 0x1f,
    'vCVTPS2PH': 0x7,
    'vDPPD': 0x33,
    'vDPPS': 0xff,
    'vEXTRACTPS': 0x03,
    'vINSERTPS': 0xff,
    'MPSADBW': 0x7,
    'VMPSADBW': 0x3f,
    'vPALIGNR': 0x3f,
    'vPBLENDW': 0xff,
    'vPCMP[EI]STR*': 0x0f,
    'vPEXTRB': 0x0f,
    'vPEXTRW': 0x07,
    'vPEXTRD': 0x03,
    'vPEXTRQ': 0x01,
    'vPINSRB': 0x0f,
    'vPINSRW': 0x07,
    'vPINSRD': 0x03,
    'vPINSRQ': 0x01,
    'vPSHUF[DW]': 0xff,
    'vPSHUF[LH]W': 0xff,
    'vPS[LR][AL][WDQ]': 0x3f,
    'vPS[RL]LDQ': 0x1f,
    'vROUND[PS][SD]': 0x7,
    'vSHUFPD': 0x0f,
    'vSHUFPS': 0xff,
    'vAESKEYGENASSIST': 0xff,
    'VEXTRACT[FI]128': 0x01,
    'VINSERT[FI]128': 0x01,
    'VPBLENDD': 0xff,
    'VPERM2[FI]128': 0x33,
    'VPERMPD': 0xff,
    'VPERMQ': 0xff,
    'VPERMILPS': 0xff,
    'VPERMILPD': 0x0f,
    }

def strip_comments(x):
    for l in x:
        if l != '' and l[0] != '#':
            yield l

def reg_w(w):
    if w == 8:
        return 'al'
    elif w == 16:
        return 'ax'
    elif w == 32:
        return 'eax'
    elif w == 64:
        return 'rax'
    raise Exception("bad reg_w %d" % w)

def mem_w(w):
    if w == 8:
        t = "BYTE"
    elif w == 16:
        t = "WORD"
    elif w == 32:
        t = "DWORD"
    elif w == 64:
        t = "QWORD"
    elif w == 128:
        t = "XMMWORD"
    elif w == 256:
        t = "YMMWORD"
    else:
        raise Exception()

    return t + " PTR 32[rdx]"

class XMMArg():
    isxmm = True
    def __init__(self, reg, mw):
        if mw not in [0, 8, 16, 32, 64, 128, 256]:
            raise Exception("Bad /m width: %s" % w)
        self.reg = reg
        self.mw = mw
        self.ismem = mw != 0
    def regstr(self, n):
        if n < 0:
            return mem_w(self.mw)
        else:
            return "%smm%d" % (self.reg, n)

class MMArg():
    isxmm = True
    def __init__(self, mw):
        if mw not in [0, 32, 64]:
            raise Exception("Bad mem width: %s" % mw)
        self.mw = mw
        self.ismem = mw != 0
    def regstr(self, n):
        return "mm%d" % (n & 7)

def match(op, pattern):
    if pattern[0] == 'v':
        return fnmatch(op, pattern[1:]) or fnmatch(op, 'V'+pattern[1:])
    return fnmatch(op, pattern)

class ArgVSIB():
    isxmm = True
    ismem = False
    def __init__(self, reg, w):
        if w not in [32, 64]:
            raise Exception("Bad vsib width: %s" % w)
        self.w = w
        self.reg = reg
    def regstr(self, n):
        reg = "%smm%d" % (self.reg, n >> 2)
        return "[rsi + %s * %d]" % (reg, 1 << (n & 3))

class ArgImm8u():
    isxmm = False
    ismem = False
    def __init__(self, op):
        for k, v in imask.items():
            if match(op, k):
                self.mask = imask[k];
                return
        raise Exception("Unknown immediate")
    def vals(self):
        mask = self.mask
        yield 0
        n = 0
        while n != mask:
            n += 1
            while (n & ~mask) != 0:
                n += (n & ~mask)
            yield n

class ArgRM():
    isxmm = False
    def __init__(self, rw, mw):
        if rw not in [8, 16, 32, 64]:
            raise Exception("Bad r/w width: %s" % w)
        if mw not in [0, 8, 16, 32, 64]:
            raise Exception("Bad r/w width: %s" % w)
        self.rw = rw
        self.mw = mw
        self.ismem = mw != 0
    def regstr(self, n):
        if n < 0:
            return mem_w(self.mw)
        else:
            return reg_w(self.rw)

class ArgMem():
    isxmm = False
    ismem = True
    def __init__(self, w):
        if w not in [8, 16, 32, 64, 128, 256]:
            raise Exception("Bad mem width: %s" % w)
        self.w = w
    def regstr(self, n):
        return mem_w(self.w)

class SkipInstruction(Exception):
    pass

def ArgGenerator(arg, op):
    if arg[:3] == 'xmm' or arg[:3] == "ymm":
        if "/" in arg:
            r, m = arg.split('/')
            if (m[0] != 'm'):
                raise Exception("Expected /m: %s", arg)
            return XMMArg(arg[0], int(m[1:]));
        else:
            return XMMArg(arg[0], 0);
    elif arg[:2] == 'mm':
        if "/" in arg:
            r, m = arg.split('/')
            if (m[0] != 'm'):
                raise Exception("Expected /m: %s", arg)
            return MMArg(int(m[1:]));
        else:
            return MMArg(0);
    elif arg[:4] == 'imm8':
        return ArgImm8u(op);
    elif arg == '<XMM0>':
        return None
    elif arg[0] == 'r':
        if '/m' in arg:
            r, m = arg.split('/')
            if (m[0] != 'm'):
                raise Exception("Expected /m: %s", arg)
            mw = int(m[1:])
            if r == 'r':
                rw = mw
            else:
                rw = int(r[1:])
            return ArgRM(rw, mw)

        return ArgRM(int(arg[1:]), 0);
    elif arg[0] == 'm':
        return ArgMem(int(arg[1:]))
    elif arg[:2] == 'vm':
        return ArgVSIB(arg[-1], int(arg[2:-1]))
    else:
        raise Exception("Unrecognised arg: %s", arg)

class InsnGenerator:
    def __init__(self, op, args):
        self.op = op
        if op[-2:] in ["PH", "PS", "PD", "SS", "SD"]:
            if op[-1] == 'H':
                self.optype = 'F16'
            elif op[-1] == 'S':
                self.optype = 'F32'
            else:
                self.optype = 'F64'
        else:
            self.optype = 'I'

        try:
            self.args = list(ArgGenerator(a, op) for a in args)
            if not any((x.isxmm for x in self.args)):
                raise SkipInstruction
            if len(self.args) > 0 and self.args[-1] is None:
                self.args = self.args[:-1]
        except SkipInstruction:
            raise
        except Exception as e:
            raise Exception("Bad arg %s: %s" % (op, e))

    def gen(self):
        regs = (10, 11, 12)
        dest = 9

        nreg = len(self.args)
        if nreg == 0:
            yield self.op
            return
        if isinstance(self.args[-1], ArgImm8u):
            nreg -= 1
            immarg = self.args[-1]
        else:
            immarg = None
        memarg = -1
        for n, arg in enumerate(self.args):
            if arg.ismem:
                memarg = n

        if (self.op.startswith("VGATHER") or self.op.startswith("VPGATHER")):
            if "GATHERD" in self.op:
                ireg = 13 << 2
            else:
                ireg = 14 << 2
            regset = [
                (dest, ireg | 0, regs[0]),
                (dest, ireg | 1, regs[0]),
                (dest, ireg | 2, regs[0]),
                (dest, ireg | 3, regs[0]),
                ]
            if memarg >= 0:
                raise Exception("vsib with memory: %s" % self.op)
        elif nreg == 1:
            regset = [(regs[0],)]
            if memarg == 0:
                regset += [(-1,)]
        elif nreg == 2:
            regset = [
                (regs[0], regs[1]),
                (regs[0], regs[0]),
                ]
            if memarg == 0:
                regset += [(-1, regs[0])]
            elif memarg == 1:
                regset += [(dest, -1)]
        elif nreg == 3:
            regset = [
                (dest, regs[0], regs[1]),
                (dest, regs[0], regs[0]),
                (regs[0], regs[0], regs[1]),
                (regs[0], regs[1], regs[0]),
                (regs[0], regs[0], regs[0]),
                ]
            if memarg == 2:
                regset += [
                    (dest, regs[0], -1),
                    (regs[0], regs[0], -1),
                    ]
            elif memarg > 0:
                raise Exception("Memarg %d" % memarg)
        elif nreg == 4:
            regset = [
                (dest, regs[0], regs[1], regs[2]),
                (dest, regs[0], regs[0], regs[1]),
                (dest, regs[0], regs[1], regs[0]),
                (dest, regs[1], regs[0], regs[0]),
                (dest, regs[0], regs[0], regs[0]),
                (regs[0], regs[0], regs[1], regs[2]),
                (regs[0], regs[1], regs[0], regs[2]),
                (regs[0], regs[1], regs[2], regs[0]),
                (regs[0], regs[0], regs[0], regs[1]),
                (regs[0], regs[0], regs[1], regs[0]),
                (regs[0], regs[1], regs[0], regs[0]),
                (regs[0], regs[0], regs[0], regs[0]),
                ]
            if memarg == 2:
                regset += [
                    (dest, regs[0], -1, regs[1]),
                    (dest, regs[0], -1, regs[0]),
                    (regs[0], regs[0], -1, regs[1]),
                    (regs[0], regs[1], -1, regs[0]),
                    (regs[0], regs[0], -1, regs[0]),
                    ]
            elif memarg > 0:
                raise Exception("Memarg4 %d" % memarg)
        else:
            raise Exception("Too many regs: %s(%d)" % (self.op, nreg))

        for regv in regset:
            argstr = []
            for i in range(nreg):
                arg = self.args[i]
                argstr.append(arg.regstr(regv[i]))
            if immarg is None:
                yield self.op + ' ' + ','.join(argstr)
            else:
                for immval in immarg.vals():
                    yield self.op + ' ' + ','.join(argstr) + ',' + str(immval)

def split0(s):
    if s == '':
        return []
    return s.split(',')

def main():
    n = 0
    if len(sys.argv) != 3:
        print("Usage: test-avx.py x86.csv test-avx.h")
        exit(1)
    csvfile = open(sys.argv[1], 'r', newline='')
    with open(sys.argv[2], "w") as outf:
        outf.write("// Generated by test-avx.py. Do not edit.\n")
        for row in csv.reader(strip_comments(csvfile)):
            insn = row[0].replace(',', '').split()
            if insn[0] in ignore:
                continue
            cpuid = row[6]
            if cpuid in archs:
                try:
                    g = InsnGenerator(insn[0], insn[1:])
                    for insn in g.gen():
                        outf.write('TEST(%d, "%s", %s)\n' % (n, insn, g.optype))
                        n += 1
                except SkipInstruction:
                    pass
        outf.write("#undef TEST\n")
        csvfile.close()

if __name__ == "__main__":
    main()
