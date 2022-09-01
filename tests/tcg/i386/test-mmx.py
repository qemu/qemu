#! /usr/bin/env python3

# Generate test-avx.h from x86.csv

import csv
import sys
from fnmatch import fnmatch

ignore = set(["EMMS", "FEMMS", "FISTTP",
    "LDMXCSR", "VLDMXCSR", "STMXCSR", "VSTMXCSR"])

imask = {
    'PALIGNR': 0x3f,
    'PEXTRB': 0x0f,
    'PEXTRW': 0x07,
    'PEXTRD': 0x03,
    'PEXTRQ': 0x01,
    'PINSRB': 0x0f,
    'PINSRW': 0x07,
    'PINSRD': 0x03,
    'PINSRQ': 0x01,
    'PSHUF[DW]': 0xff,
    'PSHUF[LH]W': 0xff,
    'PS[LR][AL][WDQ]': 0x3f,
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
    else:
        raise Exception()

    return t + " PTR 32[rdx]"

class MMArg():
    isxmm = True

    def __init__(self, mw):
        if mw not in [0, 32, 64]:
            raise Exception("Bad /m width: %s" % w)
        self.mw = mw
        self.ismem = mw != 0
    def regstr(self, n):
        if n < 0:
            return mem_w(self.mw)
        else:
            return "mm%d" % (n, )

def match(op, pattern):
    return fnmatch(op, pattern)

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
    if arg[:2] == 'mm':
        if "/" in arg:
            r, m = arg.split('/')
            if (m[0] != 'm'):
                raise Exception("Expected /m: %s", arg)
            return MMArg(int(m[1:]));
        else:
            return MMArg(0);
    elif arg[:4] == 'imm8':
        return ArgImm8u(op);
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
    else:
        raise SkipInstruction

class InsnGenerator:
    def __init__(self, op, args):
        self.op = op
        if op[0:2] == "PF":
            self.optype = 'F32'
        else:
            self.optype = 'I'

        try:
            self.args = list(ArgGenerator(a, op) for a in args)
            if len(self.args) > 0 and self.args[-1] is None:
                self.args = self.args[:-1]
        except SkipInstruction:
            raise
        except Exception as e:
            raise Exception("Bad arg %s: %s" % (op, e))

    def gen(self):
        regs = (5, 6, 7)
        dest = 4

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

        if nreg == 1:
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
    if len(sys.argv) <= 3:
        print("Usage: test-mmx.py x86.csv test-mmx.h CPUID...")
        exit(1)
    csvfile = open(sys.argv[1], 'r', newline='')
    archs = sys.argv[3:]
    with open(sys.argv[2], "w") as outf:
        outf.write("// Generated by test-mmx.py. Do not edit.\n")
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
