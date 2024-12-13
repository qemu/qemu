from cffi import FFI
from os.path import join, realpath
import gdb
import glob
import re
import tree_sitter_c as tsc
from tree_sitter import Language, Parser

root = "../../../.."
plugins_dir = join(root, "panda/plugins")

C_LANGUAGE = Language(tsc.language())
parser = Parser(C_LANGUAGE)


def traverse_tree(tree):
	cursor = tree.walk()

	visited_children = False
	while True:
		if not visited_children:
			yield cursor.node
			if not cursor.goto_first_child():
				visited_children = True
		elif cursor.goto_next_sibling():
			visited_children = False
		elif not cursor.goto_parent():
			break

def remove_functions(code):
	tree = parser.parse(bytes(code, 'utf8'))
	remove_lines = []
	for node in traverse_tree(tree):
		if node.type == 'function_definition':
			remove_lines.append((node.start_point[0], node.end_point[0]))
	
	codelines = code.split("\n")
	code = code.split("\n")
	newcode = []
	for line in range(len(code)):
		if any([start <= line <= end for start, end in remove_lines]):
			continue
		else:
			newcode.append(codelines[line])
	return "\n".join(newcode)

def gdb_resolve_issue(code):
	cmd = code.decode(errors="ignore").replace('\n',' ')
	print(f"cmd={cmd}")
	cmd = cmd.replace("__fd_mask", "unsigned int")	
	result = gdb.execute(f"p {cmd}",to_string=True).split(" = ")[1].strip()
	return result.encode()
	
def simplify_brackets(code):
	code_bytes = bytes(code, 'utf8')
	tree = parser.parse(code_bytes)
	regex_replace = {}
	for node in traverse_tree(tree):
		if node.type == 'array_declarator':
			if nn := node.children_by_field_name("size"):
				if nn[0].type in ['number_literal', 'identifier']:
					continue
				elif nn[0].type in ['binary_expression', 'parenthesized_expression', 'conditional_expression']:
					missing = nn[0].text
					regex_replace[missing] = gdb_resolve_issue(missing)
					print(f"Replacing {missing} with {regex_replace[missing]}")
				else:
					breakpoint()
		elif node.type == 'enumerator':
			if nn := node.children_by_field_name("value"):
				if nn[0].type in ['unary_expression', 'conditional_expression', 'parenthesized_expression']:
					missing = nn[0].text
					regex_replace[missing] = gdb_resolve_issue(missing) 
					print(f"Replacing {missing} with {regex_replace[missing]}")
				elif nn[0].type in ['number_literal', 'binary_expression', 'identifier', 'char_literal']:
					 continue
				else:
					breakpoint()
	
	for c in sorted(regex_replace, key=lambda x: -len(x)):
		code_bytes = code_bytes.replace(c, regex_replace[c])
	return code_bytes.decode("utf8", errors="ignore")

def get_functions(prefixes):
	functions = set()
	for x in prefixes:
		g = gdb.execute(f"info functions {x}", to_string=True) 
		for f in g.split("\n"):
			if f.endswith(";"):
				functions.add(f.split(":")[1].strip())
	return functions


def compile_target(arch, target):
	ffibuilder = FFI()
	
	def arch_to_generic(arch):
		if arch == "x86_64":
			return "i386"
		elif "mips" in arch:
			return "mips"
		elif arch == "aarch64":
			return "arm"
		return arch
	arch_trans = arch_to_generic(arch)
	includes = [
		"panda/include",
		"build",
		"include",
		"/usr/include/glib-2.0",
		"/usr/lib/x86_64-linux-gnu/glib-2.0/include",
		f"target/{arch_trans}",
		f"include/tcg/{arch}",
		f"tcg/i386",
		f"host/include/i386/",
		"include/tcg",
		"",
		"contrib/plugins",
		"include/qemu",
	]

	
	source = f"""
	typedef int __builtin_va_list;
	#define __attribute__(x)
	#define __restrict
	#define __inline
	#define __USE_GNU
	#define _BITS_STDIO2_H
	#define __USE_FORTIFY_LEVEL 0
	#define __STRINGS_FORTIFIED
	#define _BITS_STRING_FORTIFIED_H
	#define __G_UTILS_H__
	#define __int128_t int
	#define __uint128_t int
	#define __extension__
	#define __asm__(...)
	#define _Static_assert(...)
	#define __thread
	#define asm(...)
	#define COMPILING_PER_TARGET 1
	#define CONFIG_TARGET "{arch}-{target}-config-target.h"
	#define CONFIG_DEVICES "{arch}-{target}-config-devices.h"
	typedef void* FILE;
	#define __struct_FILE_defined 1
	#include <stdio.h>
	#include <stdbool.h>
	#include <setjmp.h>
	#include <glib.h>
	#include "qemu/osdep.h"
	#include "qemu/typedefs.h"
	#include "qemu/compiler.h"
	#include "exec/hwaddr.h"
	#include "exec/cpu-common.h"
	#include "{arch}-{target}-config-target.h"
	#include "target/{arch_trans}/cpu-param.h"
	#include "exec/target_long.h"
	#include "panda/types.h"
	#include "panda/callbacks/cb-defs.h"
	#include "panda/plugin.h"
	#include "panda/common.h"
	#include "exec/cpu-common.h"
	#include "panda/panda_api.h"
	#include "plugins/plugin.h"
	#include "sysemu/runstate.h"
	"""
 
	# add plugin int_fns
	for int_fns in glob.glob(f"{plugins_dir}/*/*_int_fns.h"):
		source += f'#include "{int_fns}"\n'
     
	
	target_name = target.replace("-", "_")

	ffibuilder.set_source(f"_pandare_ffi_{arch}_{target_name}", None)
	
	from subprocess import check_output
	
	with open("simple.c","w") as f:
		f.write(source)
	includes = " ".join([f"-I{realpath(join(root, include))}" for include in includes])
	check_output(f"/usr/bin/gcc -E -pthread -Wno-unused-result -Wsign-compare -DNDEBUG -g -fwrapv -O3 -Wall -fPIC -UNDEBUG {includes} simple.c -o simple.out -m64 -Wall -Winvalid-pch -std=gnu11 -O2 -g -fstack-protector-strong -Wempty-body -Wendif-labels -Wexpansion-to-defined -Wformat-security -Wformat-y2k -Wignored-qualifiers -Wimplicit-fallthrough=2 -Winit-self -Wmissing-format-attribute -Wmissing-prototypes -Wnested-externs -Wold-style-declaration -Wold-style-definition -Wredundant-decls -Wshadow=local -Wstrict-prototypes -Wtype-limits -Wundef -Wvla -Wwrite-strings -Wno-missing-include-dirs -Wno-psabi -Wno-shift-negative-value".split(" "))
	
	total = open("simple.out").read().split("\n")
	
	def process_line(line):
		import re
		line = re.sub(r'__attribute__\s*\(\(.*?\)\)', '', line)
		return line

	
	total = "\n".join([process_line(x) for x in total if x and not x.startswith("#")])
	total = remove_functions(total)
	total = simplify_brackets(total)
	with open("simple.out.processed","w") as f:
		f.write(total)
	ffibuilder.cdef(total, override=True)
	ffibuilder.compile(debug=True)


def get_current_file():
	f = gdb.execute("info files", to_string=True)
	return f.split("Local exec file:")[1].split("',")[0].strip()[1:]
	

def get_context():
	curfile = get_current_file()
	regex = r"libpanda-(?P<arch>\S*)-(?P<target>softmmu|linux-user).so"
	s = re.search(regex, curfile)
	arch, target = s.group("arch"), s.group("target")
	bits = gdb.execute("ptype target_ulong", to_string=True)
	if "unsigned long" in bits:
		bits = 64
	else:
		bits = 32
	return {"arch": arch, 
		 	"target": target, 
			"bits": bits, 
			"file": curfile}

class ExtractTypes(gdb.Command):
	def __init__(self):
		super(ExtractTypes, self).__init__("extract_types", gdb.COMMAND_DATA)

	def invoke(self, arg, from_tty):
		global file_out
		gdb.execute("set pagination off")
		ctx = get_context()
		compile_target(ctx["arch"], ctx["target"])

ExtractTypes()