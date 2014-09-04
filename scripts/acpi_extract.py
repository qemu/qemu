#!/usr/bin/python
# Copyright (C) 2011 Red Hat, Inc., Michael S. Tsirkin <mst@redhat.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <http://www.gnu.org/licenses/>.

# Process mixed ASL/AML listing (.lst file) produced by iasl -l
# Locate and execute ACPI_EXTRACT directives, output offset info
#
# Documentation of ACPI_EXTRACT_* directive tags:
#
# These directive tags output offset information from AML for BIOS runtime
# table generation.
# Each directive is of the form:
# ACPI_EXTRACT_<TYPE> <array_name> <Operator> (...)
# and causes the extractor to create an array
# named <array_name> with offset, in the generated AML,
# of an object of a given type in the following <Operator>.
#
# A directive must fit on a single code line.
#
# Object type in AML is verified, a mismatch causes a build failure.
#
# Directives and operators currently supported are:
# ACPI_EXTRACT_NAME_DWORD_CONST - extract a Dword Const object from Name()
# ACPI_EXTRACT_NAME_WORD_CONST - extract a Word Const object from Name()
# ACPI_EXTRACT_NAME_BYTE_CONST - extract a Byte Const object from Name()
# ACPI_EXTRACT_METHOD_STRING - extract a NameString from Method()
# ACPI_EXTRACT_NAME_STRING - extract a NameString from Name()
# ACPI_EXTRACT_PROCESSOR_START - start of Processor() block
# ACPI_EXTRACT_PROCESSOR_STRING - extract a NameString from Processor()
# ACPI_EXTRACT_PROCESSOR_END - offset at last byte of Processor() + 1
# ACPI_EXTRACT_PKG_START - start of Package block
#
# ACPI_EXTRACT_ALL_CODE - create an array storing the generated AML bytecode
#
# ACPI_EXTRACT is not allowed anywhere else in code, except in comments.

import re;
import sys;
import fileinput;

aml = []
asl = []
output = {}
debug = ""

class asl_line:
    line = None
    lineno = None
    aml_offset = None

def die(diag):
    sys.stderr.write("Error: %s; %s\n" % (diag, debug))
    sys.exit(1)

#Store an ASL command, matching AML offset, and input line (for debugging)
def add_asl(lineno, line):
    l = asl_line()
    l.line = line
    l.lineno = lineno
    l.aml_offset = len(aml)
    asl.append(l)

#Store an AML byte sequence
#Verify that offset output by iasl matches # of bytes so far
def add_aml(offset, line):
    o = int(offset, 16);
    # Sanity check: offset must match size of code so far
    if (o != len(aml)):
        die("Offset 0x%x != 0x%x" % (o, len(aml)))
    # Strip any trailing dots and ASCII dump after "
    line = re.sub(r'\s*\.*\s*".*$',"", line)
    # Strip traling whitespace
    line = re.sub(r'\s+$',"", line)
    # Strip leading whitespace
    line = re.sub(r'^\s+',"", line)
    # Split on whitespace
    code = re.split(r'\s+', line)
    for c in code:
        # Require a legal hex number, two digits
        if (not(re.search(r'^[0-9A-Fa-f][0-9A-Fa-f]$', c))):
            die("Unexpected octet %s" % c);
        aml.append(int(c, 16));

# Process aml bytecode array, decoding AML
def aml_pkglen_bytes(offset):
    # PkgLength can be multibyte. Bits 8-7 give the # of extra bytes.
    pkglenbytes = aml[offset] >> 6;
    return pkglenbytes + 1

def aml_pkglen(offset):
    pkgstart = offset
    pkglenbytes = aml_pkglen_bytes(offset)
    pkglen = aml[offset] & 0x3F
    # If multibyte, first nibble only uses bits 0-3
    if ((pkglenbytes > 1) and (pkglen & 0x30)):
        die("PkgLen bytes 0x%x but first nibble 0x%x expected 0x0X" %
            (pkglen, pkglen))
    offset += 1
    pkglenbytes -= 1
    for i in range(pkglenbytes):
        pkglen |= aml[offset + i] << (i * 8 + 4)
    if (len(aml) < pkgstart + pkglen):
        die("PckgLen 0x%x at offset 0x%x exceeds AML size 0x%x" %
            (pkglen, offset, len(aml)))
    return pkglen

# Given method offset, find its NameString offset
def aml_method_string(offset):
    #0x14 MethodOp PkgLength NameString MethodFlags TermList
    if (aml[offset] != 0x14):
        die( "Method offset 0x%x: expected 0x14 actual 0x%x" %
             (offset, aml[offset]));
    offset += 1;
    pkglenbytes = aml_pkglen_bytes(offset)
    offset += pkglenbytes;
    return offset;

# Given name offset, find its NameString offset
def aml_name_string(offset):
    #0x08 NameOp NameString DataRef
    if (aml[offset] != 0x08):
        die( "Name offset 0x%x: expected 0x08 actual 0x%x" %
             (offset, aml[offset]));
    offset += 1
    # Block Name Modifier. Skip it.
    if (aml[offset] == 0x5c or aml[offset] == 0x5e):
        offset += 1
    return offset;

# Given data offset, find 8 byte buffer offset
def aml_data_buffer8(offset):
    #0x08 NameOp NameString DataRef
    expect = [0x11, 0x0B, 0x0A, 0x08]
    if (aml[offset:offset+4] != expect):
        die( "Name offset 0x%x: expected %s actual %s" %
             (offset, aml[offset:offset+4], expect))
    return offset + len(expect)

# Given data offset, find dword const offset
def aml_data_dword_const(offset):
    #0x08 NameOp NameString DataRef
    if (aml[offset] != 0x0C):
        die( "Name offset 0x%x: expected 0x0C actual 0x%x" %
             (offset, aml[offset]));
    return offset + 1;

# Given data offset, find word const offset
def aml_data_word_const(offset):
    #0x08 NameOp NameString DataRef
    if (aml[offset] != 0x0B):
        die( "Name offset 0x%x: expected 0x0B actual 0x%x" %
             (offset, aml[offset]));
    return offset + 1;

# Given data offset, find byte const offset
def aml_data_byte_const(offset):
    #0x08 NameOp NameString DataRef
    if (aml[offset] != 0x0A):
        die( "Name offset 0x%x: expected 0x0A actual 0x%x" %
             (offset, aml[offset]));
    return offset + 1;

# Find name'd buffer8
def aml_name_buffer8(offset):
    return aml_data_buffer8(aml_name_string(offset) + 4)

# Given name offset, find dword const offset
def aml_name_dword_const(offset):
    return aml_data_dword_const(aml_name_string(offset) + 4)

# Given name offset, find word const offset
def aml_name_word_const(offset):
    return aml_data_word_const(aml_name_string(offset) + 4)

# Given name offset, find byte const offset
def aml_name_byte_const(offset):
    return aml_data_byte_const(aml_name_string(offset) + 4)

def aml_device_start(offset):
    #0x5B 0x82 DeviceOp PkgLength NameString
    if ((aml[offset] != 0x5B) or (aml[offset + 1] != 0x82)):
        die( "Name offset 0x%x: expected 0x5B 0x82 actual 0x%x 0x%x" %
             (offset, aml[offset], aml[offset + 1]));
    return offset

def aml_device_string(offset):
    #0x5B 0x82 DeviceOp PkgLength NameString
    start = aml_device_start(offset)
    offset += 2
    pkglenbytes = aml_pkglen_bytes(offset)
    offset += pkglenbytes
    return offset

def aml_device_end(offset):
    start = aml_device_start(offset)
    offset += 2
    pkglenbytes = aml_pkglen_bytes(offset)
    pkglen = aml_pkglen(offset)
    return offset + pkglen

def aml_processor_start(offset):
    #0x5B 0x83 ProcessorOp PkgLength NameString ProcID
    if ((aml[offset] != 0x5B) or (aml[offset + 1] != 0x83)):
        die( "Name offset 0x%x: expected 0x5B 0x83 actual 0x%x 0x%x" %
             (offset, aml[offset], aml[offset + 1]));
    return offset

def aml_processor_string(offset):
    #0x5B 0x83 ProcessorOp PkgLength NameString ProcID
    start = aml_processor_start(offset)
    offset += 2
    pkglenbytes = aml_pkglen_bytes(offset)
    offset += pkglenbytes
    return offset

def aml_processor_end(offset):
    start = aml_processor_start(offset)
    offset += 2
    pkglenbytes = aml_pkglen_bytes(offset)
    pkglen = aml_pkglen(offset)
    return offset + pkglen

def aml_package_start(offset):
    offset = aml_name_string(offset) + 4
    # 0x12 PkgLength NumElements PackageElementList
    if (aml[offset] != 0x12):
        die( "Name offset 0x%x: expected 0x12 actual 0x%x" %
             (offset, aml[offset]));
    offset += 1
    return offset + aml_pkglen_bytes(offset) + 1

lineno = 0
for line in fileinput.input():
    # Strip trailing newline
    line = line.rstrip();
    # line number and debug string to output in case of errors
    lineno = lineno + 1
    debug = "input line %d: %s" % (lineno, line)
    #ASL listing: space, then line#, then ...., then code
    pasl = re.compile('^\s+([0-9]+)(:\s\s|\.\.\.\.)\s*')
    m = pasl.search(line)
    if (m):
        add_asl(lineno, pasl.sub("", line));
    # AML listing: offset in hex, then ...., then code
    paml = re.compile('^([0-9A-Fa-f]+)(:\s\s|\.\.\.\.)\s*')
    m = paml.search(line)
    if (m):
        add_aml(m.group(1), paml.sub("", line))

# Now go over code
# Track AML offset of a previous non-empty ASL command
prev_aml_offset = -1
for i in range(len(asl)):
    debug = "input line %d: %s" % (asl[i].lineno, asl[i].line)

    l = asl[i].line

    # skip if not an extract directive
    a = len(re.findall(r'ACPI_EXTRACT', l))
    if (not a):
        # If not empty, store AML offset. Will be used for sanity checks
        # IASL seems to put {}. at random places in the listing.
        # Ignore any non-words for the purpose of this test.
        m = re.search(r'\w+', l)
        if (m):
                prev_aml_offset = asl[i].aml_offset
        continue

    if (a > 1):
        die("Expected at most one ACPI_EXTRACT per line, actual %d" % a)

    mext = re.search(r'''
                      ^\s* # leading whitespace
                      /\*\s* # start C comment
                      (ACPI_EXTRACT_\w+) # directive: group(1)
                      \s+ # whitspace separates directive from array name
                      (\w+) # array name: group(2)
                      \s*\*/ # end of C comment
                      \s*$ # trailing whitespace
                      ''', l, re.VERBOSE)
    if (not mext):
        die("Stray ACPI_EXTRACT in input")

    # previous command must have produced some AML,
    # otherwise we are in a middle of a block
    if (prev_aml_offset == asl[i].aml_offset):
        die("ACPI_EXTRACT directive in the middle of a block")

    directive = mext.group(1)
    array = mext.group(2)
    offset = asl[i].aml_offset

    if (directive == "ACPI_EXTRACT_ALL_CODE"):
        if array in output:
            die("%s directive used more than once" % directive)
        output[array] = aml
        continue
    if (directive == "ACPI_EXTRACT_NAME_BUFFER8"):
        offset = aml_name_buffer8(offset)
    elif (directive == "ACPI_EXTRACT_NAME_DWORD_CONST"):
        offset = aml_name_dword_const(offset)
    elif (directive == "ACPI_EXTRACT_NAME_WORD_CONST"):
        offset = aml_name_word_const(offset)
    elif (directive == "ACPI_EXTRACT_NAME_BYTE_CONST"):
        offset = aml_name_byte_const(offset)
    elif (directive == "ACPI_EXTRACT_NAME_STRING"):
        offset = aml_name_string(offset)
    elif (directive == "ACPI_EXTRACT_METHOD_STRING"):
        offset = aml_method_string(offset)
    elif (directive == "ACPI_EXTRACT_DEVICE_START"):
        offset = aml_device_start(offset)
    elif (directive == "ACPI_EXTRACT_DEVICE_STRING"):
        offset = aml_device_string(offset)
    elif (directive == "ACPI_EXTRACT_DEVICE_END"):
        offset = aml_device_end(offset)
    elif (directive == "ACPI_EXTRACT_PROCESSOR_START"):
        offset = aml_processor_start(offset)
    elif (directive == "ACPI_EXTRACT_PROCESSOR_STRING"):
        offset = aml_processor_string(offset)
    elif (directive == "ACPI_EXTRACT_PROCESSOR_END"):
        offset = aml_processor_end(offset)
    elif (directive == "ACPI_EXTRACT_PKG_START"):
        offset = aml_package_start(offset)
    else:
        die("Unsupported directive %s" % directive)

    if array not in output:
        output[array] = []
    output[array].append(offset)

debug = "at end of file"

def get_value_type(maxvalue):
    #Use type large enough to fit the table
    if (maxvalue >= 0x10000):
            return "int"
    elif (maxvalue >= 0x100):
            return "short"
    else:
            return "char"

# Pretty print output
for array in output.keys():
    otype = get_value_type(max(output[array]))
    odata = []
    for value in output[array]:
        odata.append("0x%x" % value)
    sys.stdout.write("static unsigned %s %s[] = {\n" % (otype, array))
    sys.stdout.write(",\n".join(odata))
    sys.stdout.write('\n};\n');
