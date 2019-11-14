#!/usr/bin/env python
# Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.

import sys
import re
import string
import cStringIO

import operator
from itertools import chain
from itertools import izip
from itertools import imap

behdict = {}		# tag ->behavior
semdict = {}		# tag -> semantics
extdict = {}		# tag -> What extension an instruction belongs to (or "")
extnames = {}		# ext name -> True
attribdict = {}		# tag -> attributes
macros = {}		# macro -> macro information... FIXME for per-ext macros
attribinfo = {}		# Register information and misc
tags = []		# list of all tags

def get_macro(macname,ext=""):
	mackey = macname + ":" + ext
	if ext and mackey not in macros:
		return get_macro(macname,"")
	return macros[mackey]

# We should do this as a hash for performance,
# but to keep order let's keep it as a list.
def uniquify(seq):
	seen = set()
	seen_add = seen.add
	return [x for x in seq if x not in seen and not seen_add(x)]

regre = re.compile(r"((?<!DUP)[MNORCPQXSGVZA])([stuvwxyzdefg]+)([.]?[LlHh]?)(\d+S?)")
immre = re.compile(r"[#]([rRsSuUm])(\d+)(?:[:](\d+))?")
detectea = re.compile(r"\bEA\b")

def gen_decl_ea(f):
	f.write( "size4u_t EA;\n")


finished_macros = set()

def expand_macro_attribs(macro,allmac_re):
	if macro.key not in finished_macros:
		# Get a list of all things that might be macros
		l = allmac_re.findall(macro.beh)
		for submacro in l:
			if not submacro: continue
			if not get_macro(submacro,macro.ext):
				raise Exception("Couldn't find macro: <%s>" % l)
			macro.attribs |= expand_macro_attribs(get_macro(submacro,macro.ext),allmac_re)
		finished_macros.add(macro.key)
	return macro.attribs

immextre = re.compile(r'f(MUST_)?IMMEXT[(]([UuSsRr])')
def calculate_attribs():
	# Recurse down macros, find attributes from sub-macros
	macroValues = macros.values()
	allmacros_restr = "|".join(set([ m.re.pattern for m in macroValues ]))
	allmacros_re = re.compile(allmacros_restr)
	for macro in macroValues:
		expand_macro_attribs(macro,allmacros_re)
	# Append attributes to all instructions
	for tag in tags:
		for macname in allmacros_re.findall(semdict[tag]):
			if not macname: continue
			macro = get_macro(macname,extdict[tag])
			attribdict[tag] |= set(macro.attribs)
		m = immextre.search(semdict[tag])
		if m:
			if m.group(2).isupper():
				attrib = 'A_EXT_UPPER_IMMED'
			elif m.group(2).islower():
				attrib = 'A_EXT_LOWER_IMMED'
			else:
				raise "Not a letter: %s (%s)" % (m.group(1),tag)
			if not attrib in attribdict[tag]:
				attribdict[tag].add(attrib)

def SEMANTICS(tag, beh, sem):
	#print tag,beh,sem
	extdict[tag] = ""
	behdict[tag] = beh
	semdict[tag] = sem
	attribdict[tag] = set()
	tags.append(tag)        # dicts have no order, this is for order

def EXT_SEMANTICS(ext, tag, beh, sem):
	#print tag,beh,sem
	extnames[ext] = True
	extdict[tag] = ext
	behdict[tag] = beh
	semdict[tag] = sem
	attribdict[tag] = set()
	tags.append(tag)        # dicts have no order, this is for order


def ATTRIBUTES(tag,attribstring):
	attribstring = attribstring.replace("ATTRIBS","").replace("(","").replace(")","")
	if not attribstring:
		return
	attribs = attribstring.split(",")
	for attrib in attribs:
		attribdict[tag].add(attrib.strip())

class Macro(object):
	__slots__ = ['key','name', 'beh', 'attribs', 're','ext']
	def __init__(self,key, name, beh, attribs,ext):
		self.key = key
		self.name = name
		self.beh = beh
		self.attribs = set(attribs)
		self.ext = ext
		self.re = re.compile("\\b" + name + "\\b")

def MACROATTRIB(macname,beh,attribstring,ext=""):
	attribstring = attribstring.replace("(","").replace(")","")
	mackey = macname + ":" + ext
	if attribstring:
		attribs = attribstring.split(",")
	else:
		attribs = []
	if macname in macros:
		raise Exception("FIXME: you need to continue distinguishing different extensions from each other? ext=<<%s>>"%ext)
	macros[mackey] = Macro(mackey,macname,beh,attribs,ext)

# read in file.  Evaluate each line: each line calls a function above

for line in open(sys.argv[1]).readlines():
	eval(line.strip())


calculate_attribs()


attribre = re.compile(r'DEF_ATTRIB\(([A-Za-z0-9_]+),([^,]*),' + 
		r'"([A-Za-z0-9_\.]*)","([A-Za-z0-9_\.]*)"\)')
for line in open(sys.argv[2]).readlines():
	if not attribre.match(line):
		#print "blah: %s" % line.strip()
		continue
	(attrib_base,descr,rreg,wreg) = attribre.findall(line)[0]
	attrib_base = 'A_' + attrib_base
	attribinfo[attrib_base] = {'rreg':rreg,'wreg':wreg,'descr':descr}

def compute_tag_regs(tag):
	return uniquify(regre.findall(behdict[tag]))

def compute_tag_immediates(tag):
	return uniquify(immre.findall(behdict[tag]))

tagregs = dict(izip(tags, map(compute_tag_regs, tags)))
tagimms = dict(izip(tags, map(compute_tag_immediates, tags)))

detectpart1 = re.compile(r"fPART1")

def need_slot(tag):
	if ('A_CONDEXEC' in attribdict[tag] or 'A_STORE' in attribdict[tag]):
		return 1
	else:
		return 0

def_helper_types={'N':'s32','O':'s32','P':'s32','M':'s32','C':'s32','S':'s32','G':'s32','R':'s32','V':'ptr','Q':'ptr','A':'TCGv_A'}
def_helper_types_pair={'R':'s64','C':'s64','S':'s64','G':'s64','V':'ptr','Q':'ptr','A':'TCGv_A'}

def gen_def_helper_type(f,regtype,regid,regno,subfield=""):
	f.write( ", %s" % (def_helper_types[regtype]))

def gen_def_helper_type_pair(f,regtype,regid,regno,subfield=""):
	f.write( ", %s" % (def_helper_types_pair[regtype]))

def gen_def_helper_type_ext(f,regtype,regid,regno,subfield=""):
	f.write(", %s" % (def_helper_types[regtype]))

def gen_def_helper_type_ext_pair(f,regtype,regid,regno,subfield=""):
	f.write( ", %s" % (def_helper_types_pair[regtype]))

def gen_def_helper_type_ext_quad(f,regtype,regid,regno,subfield=""):
	f.write( ", %s" % (def_helper_types_pair[regtype]))

def gen_def_helper_type_imm(f,name,immno):
	f.write( ", s32")



genptr_types={'P':'TCGv','M':'TCGv','C':'TCGv','S':'TCGv','G':'TCGv','R':'TCGv','V':'TCGv__V','Q':'TCGv__Q','A':'TCGv__A','N':'TCGv','O':'TCGv'}
genptr_types_pair={'R':'TCGv_i64','C':'TCGv_i64','S':'TCGv_i64','G':'TCGv_i64','V':'TCGv__V','Q':'TCGv__Q','A':'TCGv__A'}

def genptr_decl(f,regtype,regid,regno,subfield=""):
	if subfield:
		offset = subfield
	else:
		offset = "0"
	regN="%s%sN%s" % (regtype,regid,subfield)
	f.write( "DECL_%sREG_%s(%s, %s%sV%s, %s, %d, %s);\n" % \
		(regtype, regid, genptr_types[regtype], regtype, regid, subfield, regN, regno, offset))

def genptr_decl_new(f,regtype,regid,regno,subfield=""):
	if subfield:
		offset = subfield
	else:
		offset = "0"
	regN="%s%sX%s" % (regtype,regid,subfield)
	f.write( "DECL_NEW_%sREG(%s, %s%sN%s, %s, %d, %s);\n" % \
		(regtype, genptr_types[regtype], regtype, regid, subfield, regN, regno, offset))

def genptr_decl_pair(f,regtype,regid,regno,subfield=""):
	if subfield:
		offset = subfield
	else:
		offset = "0"
	regN="%s%sN%s" % (regtype,regid,subfield)
	f.write( "DECL_PAIR_%s(%s, %s%sV%s, %s, %d, %s);\n" % \
		(regid, genptr_types_pair[regtype], regtype, regid, subfield, regN, regno, offset))

def genptr_decl_ext(f,regtype,regid,regno,subfield=""):
	if subfield:
		offset = subfield
	else:
		offset = "0"
	regN="%s%sN%s" % (regtype,regid,subfield)
	f.write( "DECL_EXT_%sREG(%s%sV%s, %s, %d, %s);\n" % \
		(regtype, regtype, regid, subfield, regN, regno, offset))

def genptr_decl_ext_pair(f,regtype,regid,regno,subfield=""):
	if subfield:
		offset = subfield
	else:
		offset = "0"
	regN="%s%sN%s" % (regtype,regid,subfield)
	f.write( "DECL_EXT_%sREG_PAIR(%s%sV%s, %s, %d, %s);\n" % \
		(regtype, regtype, regid, subfield, regN, regno, offset))

def genptr_decl_ext_quad(f,regtype,regid,regno,subfield=""):
	if subfield:
		offset = subfield
	else:
		offset = "0"
	regN="%s%sN%s" % (regtype,regid,subfield)
	f.write( "DECL_EXT_%sREG_QUAD(%s%sV%s, %s, %d, %s);\n" % \
		(regtype, regtype, regid, subfield, regN, regno, offset))

def genptr_decl_imm(f,name,immno):
	f.write( "DECL_IMM(%s,%d);\n" % (name,immno,))

def gen_helper_call(f,regtype,regid,regno,subfield=""):
	if (regno > 0): f.write( ", ")
	f.write( "%s%sV%s" % (regtype,regid,subfield))

def gen_helper_call_new(f,regtype,regid,regno,subfield=""):
	if (regno > 0): f.write( ", ")
	f.write( "%s%sN%s" % (regtype,regid,subfield))

def gen_helper_call_pair(f,regtype,regid,regno,subfield=""):
	if regno != 0 : f.write( ", ")
	f.write( "%s%sV%s" % (regtype,regid,subfield))

def gen_helper_call_ext(f,regtype,regid,regno,subfield=""):
	if (regno > 0): f.write( ", ")
	f.write( "%s%sV%s" % (regtype,regid,subfield))

def gen_helper_call_ext_pair(f,regtype,regid,regno,subfield=""):
	if (regno > 0): f.write( ", ")
	f.write( "%s%sV%s" % (regtype,regid, subfield))

def gen_helper_call_ext_quad(f,regtype,regid,regno,subfield=""):
	if (regno > 0): f.write( ", ")
	f.write( "%s%sV%s" % (regtype,regid,subfield))

def gen_helper_call_imm(f,name,immno):
	f.write( ", %s" % name)

def genptr_free(f,regtype,regid,regno,subfield=""):
	regN="%s%sN%s" % (regtype,regid,subfield)
	f.write( "FREE_REG_%s(%s%sV%s);\n" % \
		(regid, regtype, regid, subfield))

def genptr_free_new(f,regtype,regid,regno,subfield=""):
	regN="%s%sN%s" % (regtype,regid,subfield)
	f.write( "FREE_NEW_%sREG(%s%sN%s);\n" % \
		(regtype, regtype, regid, subfield))

def genptr_free_pair(f,regtype,regid,regno,subfield=""):
	regN="%s%sN%s" % (regtype,regid,subfield)
	f.write( "FREE_REG_PAIR(%s%sV%s);\n" % \
		(regtype, regid, subfield))

def genptr_free_ext(f,regtype,regid,regno,subfield=""):
	regN="%s%sN%s" % (regtype,regid,subfield)
	f.write( "FREE_EXT_%sREG(%s%sV%s);\n" % \
		(regtype,regtype,regid,subfield))

def genptr_free_ext_pair(f,regtype,regid,regno,subfield=""):
	regN="%s%sN%s" % (regtype,regid,subfield)
	f.write( "FREE_EXT_%sREG_PAIR(%s%sV%s);\n" % \
		(regtype,regtype,regid,subfield))

def genptr_free_ext_quad(f,regtype,regid,regno,subfield=""):
	regN="%s%sN%s" % (regtype,regid,subfield)
	f.write( "FREE_EXT_%sREG_QUAD(%s%sV%s);\n" % \
		(regtype,regtype,regid,subfield))

def genptr_free_imm(f,name,immno):
	f.write( "FREE_IMM(%s);\n" % (name))

def genptr_src_read(f,regtype,regid,subfield=""):
	f.write( "READ_%sREG_%s(%s%sV%s, %s%sN%s);\n" % \
		(regtype,regid,regtype,regid,subfield,regtype,regid,subfield))

def genptr_src_read_new(f,regtype,regid,subfield=""):
	f.write( "READ_NEW_%sREG(%s%sN%s, %s%sX%s);\n" % \
		(regtype,regtype,regid,subfield,regtype,regid,subfield))

def genptr_src_read_pair(f,regtype,regid,subfield=""):
	f.write( "READ_%sREG_PAIR(%s%sV%s, %s%sN%s);\n" % \
		(regtype, regtype,regid,subfield,regtype,regid,subfield))

def genptr_src_read_ext(f,regtype,regid,subfield="",tmpsrc="0"):
	f.write( "READ_EXT_%sREG(%s%sN%s,%s%sV%s,%s);\n" % \
		(regtype,regtype,regid,subfield,regtype,regid,subfield,tmpsrc))

def genptr_src_read_ext_pair(f,regtype,regid,subfield="",tmpsrc="0"):
	f.write( "READ_EXT_%sREG_PAIR(%s%sN%s,%s%sV%s,%s);\n" % \
		(regtype,regtype,regid,subfield,regtype,regid,subfield,tmpsrc))

def genptr_src_read_ext_quad(f,regtype,regid,subfield="",tmpsrc="0"):
	f.write( "READ_EXT_%sREG(%s%sN%s,%s%sV%s,%s);\n" % \
		(regtype,regtype,regid,subfield,regtype,regid,subfield,tmpsrc))



def genptr_dst_write(f,regtype,regid,subfield=""):
	f.write( "WRITE_%sREG(%s%sN%s,%s%sV%s);\n" % \
		(regtype,regtype,regid,subfield,regtype,regid,subfield))

def genptr_dst_write_pair(f,regtype,regid,subfield=""):
	f.write( "WRITE_%sREG_PAIR(%s%sN%s,%s%sV%s);\n" % \
		(regtype,regtype,regid,subfield,regtype,regid,subfield))

def genptr_dst_write_ext(f,regtype,regid,subfield,newv):
	f.write( "WRITE_EXT_%sREG(%s%sN%s,%s%sV%s,%s);\n" % \
		(regtype,regtype,regid,subfield,regtype,regid,subfield,newv))

def genptr_dst_write_ext_pair(f,regtype,regid,subfield="",newv="0"):
	f.write( "WRITE_EXT_%sREG_PAIR(%s%sN%s,%s%sV%s,%s);\n" % \
		(regtype,regtype,regid,subfield,regtype,regid,subfield,newv))

def genptr_dst_write_ext_quad(f,regtype,regid,subfield="",newv="0"):
	f.write( "WRITE_EXT_%sREG_QUAD(%s%sN%s,%s%sV%s,%s);\n" % \
		(regtype,regtype,regid,subfield,regtype,regid,subfield,newv))



qemu_helper_types={'P':'int32_t','M':'int32_t','C':'int32_t','S':'int32_t','G':'int32_t','R':'int32_t','V':'TCGv_xV','Q':'TCGv_xQ','A':'TCGv_xA','N':'int32_t','O':'int32_t'}
qemu_helper_types_pair={'R':'int64_t','C':'int64_t','S':'TCGv_yS','G':'int64_t','V':'TCGv_yV','Q':'TCGv_yQ','A':'TCGv_yA'}

def gen_helper_return_type(f,regtype,regid,regno,subfield=""):
	if regno > 1 : f.write( ", ")
	f.write( "%s" % (qemu_helper_types[regtype]))

def gen_helper_return_type_pair(f,regtype,regid,regno,subfield=""):
	if regno > 1 : f.write( ", ")
	f.write( "%s" % (qemu_helper_types_pair[regtype]))



def gen_helper_arg(f,regtype,regid,regno,subfield=""):
	if regno > 0 : f.write( ", " )
	f.write( "%s %s%sV%s" % \
		(qemu_helper_types[regtype],regtype,regid,subfield))

def gen_helper_arg_new(f,regtype,regid,regno,subfield=""):
	if regno >= 0 : f.write( ", " )
	f.write( "%s %s%sN%s" % \
		(qemu_helper_types[regtype],regtype,regid,subfield))

def gen_helper_arg_pair(f,regtype,regid,regno,subfield=""):
	if regno >= 0 : f.write( ", ")
	f.write( "%s %s%sV%s" % \
		(qemu_helper_types_pair[regtype], regtype,regid,subfield))

def gen_helper_arg_ext(f,regtype,regid,regno,subfield="",newv="0"):
	if regno > 0 : f.write( ", ")
	f.write( "void *%s%sV%s_void" % (regtype,regid,subfield))

def gen_helper_arg_ext_pair(f,regtype,regid,regno,subfield="",tmpsrc="0"):
	if regno > 0 : f.write( ", ")
	f.write( "void *%s%sV_void" % (regtype,regid))

def gen_helper_arg_ext_quad(f,regtype,regid,regno,subfield="",tmpsrc="0"):
	if regno > 0 : f.write( ", " )
	f.write( "void *%s%sV%s_void" % (regtype,regid,subfield))

def gen_helper_arg_imm(f,name,immno,regno):
	if regno > 0 : f.write( ", " )
	f.write( "%s %s" % (qemu_helper_types['R'], name))



def gen_helper_dest_decl(f,regtype,regid,regno,subfield=""):
	f.write( "%s %s%sV%s = 0;\n" % \
		(qemu_helper_types[regtype],regtype,regid,subfield))

def gen_helper_dest_decl_pair(f,regtype,regid,regno,subfield=""):
	f.write( "%s %s%sV%s = 0;\n" % \
		(qemu_helper_types_pair[regtype], regtype,regid,subfield))

def gen_helper_dest_decl_ext(f,regtype,regid,subfield,newv):
	f.write( "/* %s%sV%s is *(mmvector_t*)%s%sV%s_void */\n" % \
		(regtype,regid,subfield, regtype,regid, subfield))

def gen_helper_dest_decl_ext_pair(f,regtype,regid,regno,subfield=""):
	f.write( "/* %s%sV%s is *(mmvector_pair_t*) %s%sV%s_void */\n" % \
		(regtype,regid,subfield, regtype, regid, subfield))

def gen_helper_dest_decl_ext_quad(f,regtype,regid):
	f.write( "/* %s%sV is *(mmvector_quad_t*) %s%sV_void */\n" % \
		(regtype,regid, regtype, regid))



def gen_helper_src_var_ext(f,regtype,regid,subfield,newv):
	f.write( "/* %s%sV%s is *(mmvector_t*)(%s%sV%s_void) */\n" % \
		(regtype,regid,subfield, regtype,regid,subfield))

def gen_helper_src_var_ext_pair(f,regtype,regid,subfield="",newv=""):
	f.write( "/* %s%sV%s is *(mmvector_pair_t*)(%s%sV%s_void) */\n" % \
		(regtype,regid,subfield, regtype,regid,subfield))

def gen_helper_src_var_ext_quad(f,regtype,regid,subfield="",newv=""):
	f.write( "/* %s%sV%s is *(mmvector_quad_t*)(%s%sV%s_void) */\n" % \
		(regtype,regid,subfield, regtype,regid,subfield))



def gen_helper_return(f,regtype,regid,regno,subfield=""):
	f.write( "return %s%sV%s;\n" % \
		(regtype,regid,subfield))

def gen_helper_return_pair(f,regtype,regid,regno,subfield=""):
	f.write( "return %s%sV%s;\n" % \
		(regtype,regid,subfield))



def gen_helper_dst_write_ext(f,regtype,regid,subfield,newv):
	f.write( "/* %s%sV%s is *(mmvector_t*)%s%sV%s_void */\n" % \
		(regtype,regid,subfield, regtype,regid,subfield))

def gen_helper_dst_write_ext_pair(f,regtype,regid):
	f.write( "/* %s%sV is *(mmvector_pair_t*)%s%sV_void */\n" % \
		(regtype,regid, regtype,regid))

def gen_helper_dst_write_ext_quad(f,regtype,regid):
	f.write( "/* %s%sV is *(mmvector_quad_t*)%s%sV_void */\n" % \
		(regtype,regid, regtype,regid))



def gen_decl_ea_tcg(f):
	f.write( "DECL_EA;\n")

def gen_free_ea_tcg(f):
	f.write( "FREE_EA;\n")



def gen_qemu_scalar(f, tag):
	regs = tagregs[tag]
	imms = tagimms[tag]
	numresults = 0
	numreadwrite = 0
	for regtype,regid,toss,numregs in regs:
		if (regid[0] in "defgxyz"):
			numresults += 1
		if (regid[0] in "xyz"):
			numreadwrite += 1
	f.write( 'DEF_QEMU(%s,%s,\n' % (tag,semdict[tag]))

## Start of helper declaration
	if (numresults > 1):
## The helper is bogus when there is more than one result
		f.write( 'DEF_HELPER_1(%s, void, env),\n' % tag)
	else:
		if (numresults == 0):
			if detectpart1.search(semdict[tag]):
				if (need_slot(tag)):
					f.write( 'DEF_HELPER_%s(%s' % (len(regs)+len(imms)+numreadwrite+3, tag))
				else:
					f.write( 'DEF_HELPER_%s(%s' % (len(regs)+len(imms)+numreadwrite+2, tag))
			else:
				if (need_slot(tag)):
					f.write( 'DEF_HELPER_%s(%s' % (len(regs)+len(imms)+numreadwrite+2, tag))
				else:
					f.write( 'DEF_HELPER_%s(%s' % (len(regs)+len(imms)+numreadwrite+1, tag))
			f.write( ', void' )
		else:
			if detectpart1.search(semdict[tag]):
				if (need_slot(tag)):
					f.write( 'DEF_HELPER_%s(%s' % (len(regs)+len(imms)+numreadwrite+2, tag))
				else:
					f.write( 'DEF_HELPER_%s(%s' % (len(regs)+len(imms)+numreadwrite+1, tag))
			else:
				if (need_slot(tag)):
					f.write( 'DEF_HELPER_%s(%s' % (len(regs)+len(imms)+numreadwrite+1, tag))
				else:
					f.write( 'DEF_HELPER_%s(%s' % (len(regs)+len(imms)+numreadwrite+0, tag))
		i=0
## Generate the qemu DEF_HELPER type for each result
		for regtype,regid,toss,numregs in regs:
			if (regid[0] in "defgxyz"):
				# declaration
				if (len(regid) > 2):
					#print "error: put quads here"
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "V"):  # Quads for Splatter RF in Hana
							gen_def_helper_type_ext_quad(f,regtype,regid,i)
				elif (len(regid) == 2):
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
							gen_def_helper_type_ext_pair(f,regtype,regid,i)
						else:
							gen_def_helper_type_pair(f,regtype,regid,i)
					elif regtype+regid+'N' in semdict[tag]:
						gen_decl_pair_num(f,regtype,regid,i)
					# RsV0/RsV1 should not be supported any more...
					tmp = regid[0:len(regid)/2]
					for j in range(len(regid)):
						if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
							if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
								gen_def_helper_type_ext(f,regtype,regid,i)
							else:
								gen_def_helper_type_pair(f,regtype,tmp,i,j)
				elif (len(regid) == 1):
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
							gen_def_helper_type_ext(f,regtype,regid,i)
						else:
							gen_def_helper_type(f,regtype,regid,i)
					elif regtype+regid+'N' in semdict[tag]:
						gen_def_helper_type(f,regtype,regid,i)
				else:
					print "Bad register parse: ",regtype,regid,toss,numregs
				i += 1
## Put the env between the outputs and inputs
		if (i == numresults): f.write( ', env' )
## Generate the qemu type for each input operand (regs and immediates)
		for regtype,regid,toss,numregs in regs:
			if (regid[0] in "stuvwxyz"):
				# declaration
				if (len(regid) > 2):
					#print "error: put quads here"
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "V"):  # Quads for Splatter RF in Hana
							gen_def_helper_type_ext_quad(f,regtype,regid,i)
				elif (len(regid) == 2):
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
							gen_def_helper_type_ext_pair(f,regtype,regid,i)
						else:
							gen_def_helper_type_pair(f,regtype,regid,i)
					elif regtype+regid+'N' in semdict[tag]:
						gen_decl_pair_num(f,regtype,regid,i)
					# RsV0/RsV1 should not be supported any more...
					tmp = regid[0:len(regid)/2]
					for j in range(len(regid)):
						if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
							if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
								gen_def_helper_type_ext(f,regtype,regid,i)
							else:
								gen_def_helper_type(f,regtype,tmp,i,j)
				elif (len(regid) == 1):
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
							gen_def_helper_type_ext(f,regtype,regid,i)
						else:
							gen_def_helper_type(f,regtype,regid,i)
					elif regtype+regid+'N' in semdict[tag]:
						gen_def_helper_type(f,regtype,regid,i)
				else:
					print "Bad register parse: ",regtype,regid,toss,numregs
				i += 1
		for immlett,bits,immshift in imms:
			# declaration && READ
			if (immlett.isupper()):
				i = 1
			else:
				i = 0
			gen_def_helper_type_imm(f,'%siV' % immlett, i)
## Add the argument for the instruction slot and part1 (if needed)
		if (need_slot(tag)): f.write( ', i32' )
		if detectpart1.search(semdict[tag]): f.write(' , i32' )
		f.write( '),\n' )
## End of helper declaration

## Start of the TCG generation function
	f.write( '{\n' )
	f.write( '/* %s */\n' % tag)
	if detectea.search(semdict[tag]): gen_decl_ea_tcg(f)
	i=0
	debug = 0
## Declare all the operands (regs and immediates)
	for regtype,regid,toss,numregs in regs:
		# declaration
		if (len(regid) > 2):
			#print "error: put quads here"
			if regtype+regid+'V' in semdict[tag]:
				if (regtype in "V"):  # Quads for Splatter RF in Hana
					genptr_decl_ext_quad(f,regtype,regid,i)
		elif (len(regid) == 2):
			if regtype+regid+'V' in semdict[tag]:
				if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
					genptr_decl_ext_pair(f,regtype,regid,i)
				else:
					genptr_decl_pair(f,regtype,regid,i)
			elif regtype+regid+'N' in semdict[tag]:
				genptr_decl_pair_num(f,regtype,regid,i)
			# RsV0/RsV1 should not be supported any more...
			tmp = regid[0:len(regid)/2]
			for j in range(len(regid)):
				if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
					if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
						genptr_decl_ext(f,regtype,regid,i)
					else:
						genptr_decl(f,regtype,tmp,i,j)
		elif (len(regid) == 1):
			if regtype+regid+'V' in semdict[tag]:
				if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
					genptr_decl_ext(f,regtype,regid,i)
				else:
					genptr_decl(f,regtype,regid,i)
			elif regtype+regid+'N' in semdict[tag]:
				genptr_decl_new(f,regtype,regid,i)
		else:
			print "Bad register parse: ",regtype,regid,toss,numregs
		i += 1
	for immlett,bits,immshift in imms:
		# declaration && READ
		if (immlett.isupper()):
			i = 1
		else:
			i = 0
		genptr_decl_imm(f,'%siV' % immlett, i)
	if 'A_PRIV' in attribdict[tag]:
		f.write('fCHECKFORPRIV();\n')
	if 'A_GUEST' in attribdict[tag]:
		f.write('fCHECKFORGUEST();\n')

	if 'A_FPOP' in attribdict[tag]:
		f.write('fFPOP_START();\n');

## Read all the inputs (source regs and immediates)
	for regtype,regid,toss,numregs in regs:
		if (regid[0] in "stuvwxyz"):
			# source or source/dest register
			if (len(regid) > 2):
				#print "error: put quads here"
				if regtype+regid+'V' in semdict[tag]: # Quads for Splatter RF in Hana
					if (regtype in "V"):
						if 'A_CVI_TMP_SRC' in attribdict[tag]:
							gen_source_read_ext_quad(f,regtype,regid,"","EXT_TMP")
						else:
							gen_source_read_ext_quad(f,regtype,regid)
			elif (len(regid) == 2):
				if regtype+regid+'V' in semdict[tag]:
					#JPG: Crazy case of slot 3 to slot 2 forwarding for silver
					if (regtype in "VZ") and (regid[0] in "x") and ('A_VECX_ACCFWD' in attribdict[tag]):
						gen_source_read_ext_acc_pair(f,regtype,regid)
					elif (regtype in "VZQ"): # and ('A_CVI' in attribdict[tag]):
						if 'A_CVI_TMP_SRC' in attribdict[tag]:
							gen_source_read_ext_pair(f,regtype,regid,"","EXT_TMP")
						else:
							gen_source_read_ext_pair(f,regtype,regid)
					else:
						genptr_src_read_pair(f,regtype,regid)
				tmp = regid[0:len(regid)/2]
				for j in range(len(regid)):
					if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
						genptr_src_read(f,regtype,tmp,j)
			elif (len(regid) == 1):
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "VZ") and (regid[0] in "x") and ('A_VECX_ACCFWD' in attribdict[tag]):
						gen_source_read_ext_acc(f,regtype,regid)
					# JPG: if source is a vector check, call vector version
					elif (regtype in "VZQ"): # and ('A_CVI' in attribdict[tag]):
						if 'A_CVI_TMP_SRC' in attribdict[tag]:
							genptr_src_read_ext(f,regtype,regid,"","EXT_TMP")
						else:
							genptr_src_read_ext(f,regtype,regid)
					else:
						genptr_src_read(f,regtype,regid)
				elif regtype+regid+'N' in semdict[tag]:
					genptr_src_read_new(f,regtype,regid)
			else:
				print "Bad register parse: ",regtype,regid,toss,numregs

## Generate the call to the helper
	f.write( "fWRAP_%s(" % tag)
	if detectpart1.search(semdict[tag]): f.write( 'PART1_WRAP(' )
	if (need_slot(tag)): f.write( 'SLOT_WRAP(' )
	f.write( "gen_helper_%s(" % tag)
	i=0
	for regtype,regid,toss,numregs in regs:
		if (regid[0] in "defgxyz"):
			# declaration
			if (len(regid) > 2):
				#print "error: put quads here"
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "V"):  # Quads for Splatter RF in Hana
						gen_decl_ext_quad(f,regtype,regid,i)
			elif (len(regid) == 2):
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
						gen_decl_ext_pair(f,regtype,regid,i)
					else:
						gen_helper_call_pair(f,regtype,regid,i)
				elif regtype+regid+'N' in semdict[tag]:
					gen_decl_pair_num(f,regtype,regid,i)
				# RsV0/RsV1 should not be supported any more...
				tmp = regid[0:len(regid)/2]
				for j in range(len(regid)):
					if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
						if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
							gen_decl_ext(f,regtype,regid,i)
						else:
							gen_helper_call_pair(f,regtype,tmp,i,j)
			elif (len(regid) == 1):
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
						gen_decl_ext(f,regtype,regid,i)
					else:
						gen_helper_call(f,regtype,regid,i)
				elif regtype+regid+'N' in semdict[tag]:
					gen_helper_call_new(f,regtype,regid,i)
			else:
				print "Bad register parse: ",regtype,regid,toss,numregs
			i += 1
	if ( i > 0 ): f.write( ", " )
	f.write("cpu_env")
	i=1
	for regtype,regid,toss,numregs in regs:
		if (regid[0] in "stuvwxyz"):
			# declaration
			if (len(regid) > 2):
				#print "error: put quads here"
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "V"):  # Quads for Splatter RF in Hana
						gen_decl_ext_quad(f,regtype,regid,i)
			elif (len(regid) == 2):
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
						gen_decl_ext_pair(f,regtype,regid,i)
					else:
						gen_helper_call_pair(f,regtype,regid,i)
				elif regtype+regid+'N' in semdict[tag]:
					gen_decl_pair_num(f,regtype,regid,i)
				# RsV0/RsV1 should not be supported any more...
				tmp = regid[0:len(regid)/2]
				for j in range(len(regid)):
					if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
						if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
							gen_decl_ext(f,regtype,regid,i)
						else:
							gen_helper_call(f,regtype,tmp,i,j)
			elif (len(regid) == 1):
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
						gen_decl_ext(f,regtype,regid,i)
					else:
						gen_helper_call(f,regtype,regid,i)
				elif regtype+regid+'N' in semdict[tag]:
					gen_helper_call_new(f,regtype,regid,i)
			else:
				print "Bad register parse: ",regtype,regid,toss,numregs
			i += 1
	for immlett,bits,immshift in imms:
		# declaration && READ
		if (immlett.isupper()):
			i = 1
		else:
			i = 0
		gen_helper_call_imm(f,'%siV' % immlett, i)
	if (need_slot(tag)): f.write( ", slot" )
	if detectpart1.search(semdict[tag]): f.write( ", part1" )
	f.write( ")" )
	if (need_slot(tag)): f.write( ')' )
	if detectpart1.search(semdict[tag]): f.write( ')' )
	f.write( ";,\n%s)\n" % semdict[tag] )

## Write all the outputs (destination regs)
	for regtype,regid,toss,numregs in regs:
		if (regid[0] in "defgxyz"):
			# destination or source/dest register
			if (len(regid) > 2):
				#print "error: put quads here"
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "V"): # Quads for Splatter RF in Hana
						gen_dest_write_ext_quad(f,regtype,regid)
			elif (len(regid) == 2):
				if regtype+regid+'V' in semdict[tag]:
					#JPG: Write Pair for MMVECTOR
					if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
						if 'A_CVI_TMP' in attribdict[tag]:
							gen_dest_write_ext_pair(f,regtype,regid,"","EXT_TMP")
						elif 'A_CVI_TMP_DST' in attribdict[tag]:
							gen_dest_write_ext_pair(f,regtype,regid,"","EXT_TMP")
						else:
							gen_dest_write_ext_pair(f,regtype,regid)
					else:
						genptr_dst_write_pair(f,regtype,regid)
				tmp = regid[0:len(regid)/2]
				for j in range(len(regid)):
					if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
						genptr_dst_write(f,regtype,tmp,j)
			elif (len(regid) == 1):
				if regtype+regid+'V' in semdict[tag]:
					# if dest is a vector check, call vector version
					if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
						# MMVEC if dest is a Vd.current or Vd.tmp load set the approriate flag for the log
						if 'A_CVI_NEW' in attribdict[tag]:
							gen_dest_write_ext(f,regtype,regid,"","EXT_NEW")
						elif 'A_CVI_TMP' in attribdict[tag]:
							gen_dest_write_ext(f,regtype,regid,"","EXT_TMP")
						elif 'A_CVI_TMP_DST' in attribdict[tag]:
							gen_dest_write_ext(f,regtype,regid,"","EXT_TMP")
						else:
							gen_dest_write_ext(f,regtype,regid,"","EXT_DFL")
					else:
						genptr_dst_write(f,regtype,regid)
			else:
				print "Bad register parse: ",regtype,regid,toss,numregs

	# EJP: I think here is where we can check for FP info and put flag post-function stuff
	if 'A_FPOP' in attribdict[tag]:
		f.write('fFPOP_END();\n');

## Free all the operands (regs and immediates)
	if detectea.search(semdict[tag]): gen_free_ea_tcg(f)
	for regtype,regid,toss,numregs in regs:
		# declaration
		if (len(regid) > 2):
			#print "error: put quads here"
			if regtype+regid+'V' in semdict[tag]:
				if (regtype in "V"):  # Quads for Splatter RF in Hana
					gen_decl_ext_quad(f,regtype,regid,i)
		elif (len(regid) == 2):
			if regtype+regid+'V' in semdict[tag]:
				if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
					gen_decl_ext_pair(f,regtype,regid,i)
				else:
					genptr_free_pair(f,regtype,regid,i)
			elif regtype+regid+'N' in semdict[tag]:
				gen_decl_pair_num(f,regtype,regid,i)
			# RsV0/RsV1 should not be supported any more...
			tmp = regid[0:len(regid)/2]
			for j in range(len(regid)):
				if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
					if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
						gen_decl_ext(f,regtype,regid,i)
					else:
						genptr_free(f,regtype,tmp,i,j)
		elif (len(regid) == 1):
			if regtype+regid+'V' in semdict[tag]:
				if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
					gen_decl_ext(f,regtype,regid,i)
				else:
					genptr_free(f,regtype,regid,i)
			elif regtype+regid+'N' in semdict[tag]:
				genptr_free_new(f,regtype,regid,i)
		else:
			print "Bad register parse: ",regtype,regid,toss,numregs
		i += 1
	for immlett,bits,immshift in imms:
		# declaration && READ
		if (immlett.isupper()):
			i = 1
		else:
			i = 0
		genptr_free_imm(f,'%siV' % immlett, i)

	f.write( "/* %s */\n" % tag )
	f.write( "}" )
## End of the TCG generation function

## Start of the helper definition
	f.write( ",\n" )
	if (numresults > 1):
## The helper is bogus when there is more than one result
		f.write( "void HELPER(%s)(CPUHexagonState *env) { BOGUS_HELPER(%s); }\n" % (tag, tag))
	else:
## The return type of the function is the type of the destination register
		i=0
		for regtype,regid,toss,numregs in regs:
			if (regid[0] in "defgxyz"):
				# destination or source/dest register
				if (len(regid) > 2):
					#print "error: put quads here"
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "V"): # Quads for Splatter RF in Hana
							gen_dest_write_ext_quad(f,regtype,regid)
				elif (len(regid) == 2):
					if regtype+regid+'V' in semdict[tag]:
						#JPG: Write Pair for MMVECTOR
						if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
							if 'A_CVI_TMP' in attribdict[tag]:
								gen_dest_write_ext_pair(f,regtype,regid,"","EXT_TMP")
							elif 'A_CVI_TMP_DST' in attribdict[tag]:
								gen_dest_write_ext_pair(f,regtype,regid,"","EXT_TMP")
							else:
								gen_dest_write_ext_pair(f,regtype,regid)
						else:
							gen_helper_return_type_pair(f,regtype,regid,i)
					tmp = regid[0:len(regid)/2]
					for j in range(len(regid)):
						if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
							gen_helper_return_type(f,regtype,tmp,i,j)
				elif (len(regid) == 1):
					if regtype+regid+'V' in semdict[tag]:
						# if dest is a vector check, call vector version
						if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
							# MMVEC if dest is a Vd.current or Vd.tmp load set the approriate flag for the log
							if 'A_CVI_NEW' in attribdict[tag]:
								gen_dest_write_ext(f,regtype,regid,"","EXT_NEW")
							elif 'A_CVI_TMP' in attribdict[tag]:
								gen_dest_write_ext(f,regtype,regid,"","EXT_TMP")
							elif 'A_CVI_TMP_DST' in attribdict[tag]:
								gen_dest_write_ext(f,regtype,regid,"","EXT_TMP")
							else:
								gen_dest_write_ext(f,regtype,regid,"","EXT_DFL")
						else:
							gen_helper_return_type(f,regtype,regid,i)
				else:
					print "Bad register parse: ",regtype,regid,toss,numregs
			i += 1

		if (numresults == 0):
			f.write( "void" )
		f.write( " HELPER(%s)(CPUHexagonState *env" % tag)
## Arguments to the helper function are the source regs and immediates
		i = 1
		for regtype,regid,toss,numregs in regs:
			if (regid[0] in "stuvwxyz"):
				# source or source/dest register
				if (len(regid) > 2):
					#print "error: put quads here"
					if regtype+regid+'V' in semdict[tag]: # Quads for Splatter RF in Hana
						if (regtype in "V"):
							if 'A_CVI_TMP_SRC' in attribdict[tag]:
								gen_source_read_ext_quad(f,regtype,regid,"","EXT_TMP")
							else:
								gen_source_read_ext_quad(f,regtype,regid)
				elif (len(regid) == 2):
					if regtype+regid+'V' in semdict[tag]:
						#JPG: Crazy case of slot 3 to slot 2 forwarding for silver
						if (regtype in "VZ") and (regid[0] in "x") and ('A_VECX_ACCFWD' in attribdict[tag]):
							gen_source_read_ext_acc_pair(f,regtype,regid)
						elif (regtype in "VZQ"): # and ('A_CVI' in attribdict[tag]):
							if 'A_CVI_TMP_SRC' in attribdict[tag]:
								gen_source_read_ext_pair(f,regtype,regid,"","EXT_TMP")
							else:
								gen_source_read_ext_pair(f,regtype,regid)
						else:
							gen_helper_arg_pair(f,regtype,regid,i)
					tmp = regid[0:len(regid)/2]
					for j in range(len(regid)):
						if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
							gen_helper_arg(f,regtype,tmp,i,j)
				elif (len(regid) == 1):
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "VZ") and (regid[0] in "x") and ('A_VECX_ACCFWD' in attribdict[tag]):
							gen_source_read_ext_acc(f,regtype,regid)
						# JPG: if source is a vector check, call vector version
						elif (regtype in "VZQ"): # and ('A_CVI' in attribdict[tag]):
							if 'A_CVI_TMP_SRC' in attribdict[tag]:
								gen_helper_arg_ext(f,regtype,regid,i,"","EXT_TMP")
							else:
								gen_helper_arg_ext(f,regtype,regid,i)
						else:
							gen_helper_arg(f,regtype,regid,i)
					elif regtype+regid+'N' in semdict[tag]:
						gen_helper_arg_new(f,regtype,regid,i)
				else:
					print "Bad register parse: ",regtype,regid,toss,numregs

				i += 1
		for immlett,bits,immshift in imms:
			# declaration && READ
			if (immlett.isupper()):
				j = 1
			else:
				j = 0
			gen_helper_arg_imm(f,'%siV' % immlett, j, i)
			i += 1
		if (need_slot(tag)): f.write( ", uint32_t slot" )
		if detectpart1.search(semdict[tag]): f.write( ', uint32_t part1' )
		f.write( ")\n{\n" )
		if (not need_slot(tag)): f.write( "uint32_t slot = 4; slot = slot;\n" )
## Declare the return variable
		if detectea.search(semdict[tag]): gen_decl_ea(f)
		i=0
		for regtype,regid,toss,numregs in regs:
			if (regid[0] in "defg"):
				# destination register
				if (len(regid) > 2):
					#print "error: put quads here"
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "V"): # Quads for Splatter RF in Hana
							gen_dest_write_ext_quad(f,regtype,regid)
				elif (len(regid) == 2):
					if regtype+regid+'V' in semdict[tag]:
						#JPG: Write Pair for MMVECTOR
						if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
							if 'A_CVI_TMP' in attribdict[tag]:
								gen_dest_write_ext_pair(f,regtype,regid,"","EXT_TMP")
							elif 'A_CVI_TMP_DST' in attribdict[tag]:
								gen_dest_write_ext_pair(f,regtype,regid,"","EXT_TMP")
							else:
								gen_dest_write_ext_pair(f,regtype,regid)
						else:
							gen_helper_dest_decl_pair(f,regtype,regid,i)
					tmp = regid[0:len(regid)/2]
					for j in range(len(regid)):
						if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
							gen_helper_dest_decl(f,regtype,tmp,i,j)
				elif (len(regid) == 1):
					if regtype+regid+'V' in semdict[tag]:
						# if dest is a vector check, call vector version
						if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
							# MMVEC if dest is a Vd.current or Vd.tmp load set the approriate flag for the log
							if 'A_CVI_NEW' in attribdict[tag]:
								gen_dest_write_ext(f,regtype,regid,"","EXT_NEW")
							elif 'A_CVI_TMP' in attribdict[tag]:
								gen_dest_write_ext(f,regtype,regid,"","EXT_TMP")
							elif 'A_CVI_TMP_DST' in attribdict[tag]:
								gen_dest_write_ext(f,regtype,regid,"","EXT_TMP")
							else:
								gen_dest_write_ext(f,regtype,regid,"","EXT_DFL")
						else:
							gen_helper_dest_decl(f,regtype,regid,i)
				else:
					print "Bad register parse: ",regtype,regid,toss,numregs
			i += 1
		if 'A_FPOP' in attribdict[tag]:
			f.write('fFPOP_START();\n');
		f.write( semdict[tag] )
		f.write( "\n" )
		f.write( "COUNT_HELPER(%s);\n" % tag )
		if 'A_FPOP' in attribdict[tag]:
			f.write('fFPOP_END();\n');
## Return the return variable
		i=0
		for regtype,regid,toss,numregs in regs:
			if (regid[0] in "defgxyz"):
				# destination or source/dest register
				if (len(regid) > 2):
					#print "error: put quads here"
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "V"): # Quads for Splatter RF in Hana
							gen_dest_write_ext_quad(f,regtype,regid)
				elif (len(regid) == 2):
					if regtype+regid+'V' in semdict[tag]:
						#JPG: Write Pair for MMVECTOR
						if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
							if 'A_CVI_TMP' in attribdict[tag]:
								gen_dest_write_ext_pair(f,regtype,regid,"","EXT_TMP")
							elif 'A_CVI_TMP_DST' in attribdict[tag]:
								gen_dest_write_ext_pair(f,regtype,regid,"","EXT_TMP")
							else:
								gen_dest_write_ext_pair(f,regtype,regid)
						else:
							gen_helper_return_pair(f,regtype,regid,i)
					tmp = regid[0:len(regid)/2]
					for j in range(len(regid)):
						if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
							gen_helper_return(f,regtype,tmp,i,j)
				elif (len(regid) == 1):
					if regtype+regid+'V' in semdict[tag]:
						# if dest is a vector check, call vector version
						if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
							# MMVEC if dest is a Vd.current or Vd.tmp load set the approriate flag for the log
							if 'A_CVI_NEW' in attribdict[tag]:
								gen_dest_write_ext(f,regtype,regid,"","EXT_NEW")
							elif 'A_CVI_TMP' in attribdict[tag]:
								gen_dest_write_ext(f,regtype,regid,"","EXT_TMP")
							elif 'A_CVI_TMP_DST' in attribdict[tag]:
								gen_dest_write_ext(f,regtype,regid,"","EXT_TMP")
							else:
								gen_dest_write_ext(f,regtype,regid,"","EXT_DFL")
						else:
							gen_helper_return(f,regtype,regid,i)
				else:
					print "Bad register parse: ",regtype,regid,toss,numregs
			i += 1
		f.write( "}" )
## End of the helper definition
	f.write( ")\n" )


def gen_qemu_hvx(f, tag):
	regs = tagregs[tag]
	imms = tagimms[tag]
	numresults = 0
	numscalarresults = 0
	numscalarreadwrite = 0
	for regtype,regid,toss,numregs in regs:
		if (regid[0] in "defgxyz"):
			numresults += 1
			if (regtype == 'R'):
				numscalarresults += 1
		if (regid[0] in "xyz"):
			if (regtype == 'R'):
				numscalarreadwrite += 1
	f.write( 'DEF_QEMU(%s,%s,\n' % (tag,semdict[tag]))
## Start of helper declaration
	if (numscalarresults > 1):
## The helper is bogus when there is more than one result
		f.write( 'DEF_HELPER_1(%s, void, env),\n' % tag)
	else:
		if (numscalarresults == 0):
			if detectpart1.search(semdict[tag]):
				f.write( 'DEF_HELPER_%s(%s' % (len(regs)+len(imms)+numscalarreadwrite+3, tag))
			else:
				f.write( 'DEF_HELPER_%s(%s' % (len(regs)+len(imms)+numscalarreadwrite+2, tag))
			f.write( ', void' )
		else:
			if detectpart1.search(semdict[tag]):
				f.write( 'DEF_HELPER_%s(%s' % (len(regs)+len(imms)+numscalarreadwrite+2, tag))
			else:
				f.write( 'DEF_HELPER_%s(%s' % (len(regs)+len(imms)+numscalarreadwrite+1, tag))
		i=0
## Generate the qemu type for each result
## Iterate over this list twice - emit the scalar result first, then the vector
		for regtype,regid,toss,numregs in regs:
			if (regid[0] in "defgxyz"):
				# declaration
				if (len(regid) > 2):
					#print "error: put quads here"
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "V"):  # Quads for Splatter RF in Hana
							continue
				elif (len(regid) == 2):
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
							continue
						else:
							gen_def_helper_type_pair(f,regtype,regid,i)
					elif regtype+regid+'N' in semdict[tag]:
						gen_decl_pair_num(f,regtype,regid,i)
					# RsV0/RsV1 should not be supported any more...
					tmp = regid[0:len(regid)/2]
					for j in range(len(regid)):
						if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
							if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
								continue
							else:
								gen_def_helper_type(f,regtype,tmp,i,j)
				elif (len(regid) == 1):
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
							continue
						else:
							gen_def_helper_type(f,regtype,regid,i)
					elif regtype+regid+'N' in semdict[tag]:
						gen_def_helper_type(f,regtype,regid,i)
				else:
					print "Bad register parse: ",regtype,regid,toss,numregs
				i += 1
		f.write(", env")
		i += 1
# Second pass
		for regtype,regid,toss,numregs in regs:
			if (regid[0] in "defgxyz"):
				# declaration
				if (len(regid) > 2):
					#print "error: put quads here"
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "V"):  # Quads for Splatter RF in Hana
							gen_def_helper_type_ext_quad(f,regtype,regid,i)
				elif (len(regid) == 2):
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
							gen_def_helper_type_ext_pair(f,regtype,regid,i)
						else:
							continue
					elif regtype+regid+'N' in semdict[tag]:
						continue
					# RsV0/RsV1 should not be supported any more...
					tmp = regid[0:len(regid)/2]
					for j in range(len(regid)):
						if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
							if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
								continue
							else:
								gen_def_helper_type(f,regtype,tmp,i,j)
				elif (len(regid) == 1):
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
							gen_def_helper_type_ext(f,regtype,regid,i)
						else:
							continue
					elif regtype+regid+'N' in semdict[tag]:
						continue
				else:
					print "Bad register parse: ",regtype,regid,toss,numregs
				i += 1
## Generate the qemu type for each input operand (regs and immediates)
		for regtype,regid,toss,numregs in regs:
			if (regid[0] in "stuvwxyz"):
				# declaration
				if (len(regid) > 2):
					#print "error: put quads here"
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "V"):  # Quads for Splatter RF in Hana
							# Skip the read/write vectors
							if (regid[0] in "xyz"):
								continue
							gen_def_helper_type_ext_quad(f,regtype,regid,i)
				elif (len(regid) == 2):
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
							# Skip the read/write vectors
							if (regid[0] in "xyz"):
								continue
							gen_def_helper_type_ext_pair(f,regtype,regid,i)
						else:
							gen_def_helper_type_pair(f,regtype,regid,i)
					elif regtype+regid+'N' in semdict[tag]:
						gen_decl_pair_num(f,regtype,regid,i)
					# RsV0/RsV1 should not be supported any more...
					tmp = regid[0:len(regid)/2]
					for j in range(len(regid)):
						if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
							if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
								if (regid[0] in "xyz"):
									continue
								gen_def_helper_type_ext(f,regtype,regid,i)
							else:
								gen_def_helper_type(f,regtype,tmp,i,j)
				elif (len(regid) == 1):
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
							# Skip the read/write vectors
							if (regid[0] in "xyz"):
								continue
							gen_def_helper_type_ext(f,regtype,regid,i)
						else:
							gen_def_helper_type(f,regtype,regid,i)
					elif regtype+regid+'N' in semdict[tag]:
						gen_def_helper_type(f,regtype,regid,i)
				else:
					print "Bad register parse: ",regtype,regid,toss,numregs
				i += 1
		for immlett,bits,immshift in imms:
			# declaration && READ
			if (immlett.isupper()):
				i = 1
			else:
				i = 0
			gen_def_helper_type_imm(f,'%siV' % immlett, i)
## Add the argument for the slot
		f.write( ', i32' )
		f.write( '),\n' )
## End of helper declaration

## Start of the TCG generation function
	f.write( '{\n' )
	f.write( '/* %s */\n' % tag)
	if detectea.search(semdict[tag]): gen_decl_ea_tcg(f)
	i=0
	debug = 0
## Declare all the operands (regs and immediates)
	for regtype,regid,toss,numregs in regs:
		# declaration
		if (len(regid) > 2):
			#print "error: put quads here"
			if regtype+regid+'V' in semdict[tag]:
				if (regtype in "V"):  # Quads for Splatter RF in Hana
					genptr_decl_ext_quad(f,regtype,regid,i)
		elif (len(regid) == 2):
			if regtype+regid+'V' in semdict[tag]:
				if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
					genptr_decl_ext_pair(f,regtype,regid,i)
				else:
					genptr_decl_pair(f,regtype,regid,i)
			elif regtype+regid+'N' in semdict[tag]:
				gen_decl_pair_num(f,regtype,regid,i)
			# RsV0/RsV1 should not be supported any more...
			tmp = regid[0:len(regid)/2]
			for j in range(len(regid)):
				if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
					if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
						genptr_decl_ext(f,regtype,regid,i)
					else:
						genptr_decl(f,regtype,tmp,i,j)
		elif (len(regid) == 1):
			if regtype+regid+'V' in semdict[tag]:
				if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
					genptr_decl_ext(f,regtype,regid,i)
				else:
					genptr_decl(f,regtype,regid,i)
			elif regtype+regid+'N' in semdict[tag]:
				genptr_decl_new(f,regtype,regid,i)
		else:
			print "Bad register parse: ",regtype,regid,toss,numregs
		i += 1
	for immlett,bits,immshift in imms:
		# declaration && READ
		if (immlett.isupper()):
			i = 1
		else:
			i = 0
		genptr_decl_imm(f,'%siV' % immlett, i)
	if 'A_PRIV' in attribdict[tag]:
		f.write('fCHECKFORPRIV();\n')
	if 'A_GUEST' in attribdict[tag]:
		f.write('fCHECKFORGUEST();\n')

	if 'A_FPOP' in attribdict[tag]:
		f.write('fFPOP_START();\n');

## Read all the inputs (source regs and immediates)
	for regtype,regid,toss,numregs in regs:
		if (regid[0] in "stuvwxyz"):
			# source or source/dest register
			if (len(regid) > 2):
				#print "error: put quads here"
				if regtype+regid+'V' in semdict[tag]: # Quads for Splatter RF in Hana
					if (regtype in "V"):
						if 'A_CVI_TMP_SRC' in attribdict[tag]:
							genptr_src_read_ext_quad(f,regtype,regid,"","EXT_TMP")
						else:
							genptr_src_read_ext_quad(f,regtype,regid)
			elif (len(regid) == 2):
				if regtype+regid+'V' in semdict[tag]:
					#JPG: Crazy case of slot 3 to slot 2 forwarding for silver
					if (regtype in "VZ") and (regid[0] in "x") and ('A_VECX_ACCFWD' in attribdict[tag]):
						gen_source_read_ext_acc_pair(f,regtype,regid)
					elif (regtype in "VZQ"): # and ('A_CVI' in attribdict[tag]):
						if 'A_CVI_TMP_SRC' in attribdict[tag]:
							genptr_src_read_ext_pair(f,regtype,regid,"","EXT_TMP")
						else:
							genptr_src_read_ext_pair(f,regtype,regid)
					else:
						genptr_src_read_pair(f,regtype,regid)
				tmp = regid[0:len(regid)/2]
				for j in range(len(regid)):
					if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
						genptr_src_read(f,regtype,tmp,j)
			elif (len(regid) == 1):
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "VZ") and (regid[0] in "x") and ('A_VECX_ACCFWD' in attribdict[tag]):
						gen_source_read_ext_acc(f,regtype,regid)
					# JPG: if source is a vector check, call vector version
					elif (regtype in "VZQ"): # and ('A_CVI' in attribdict[tag]):
						if 'A_CVI_TMP_SRC' in attribdict[tag]:
							genptr_src_read_ext(f,regtype,regid,"","EXT_TMP")
						else:
							genptr_src_read_ext(f,regtype,regid)
					else:
						genptr_src_read(f,regtype,regid)
				elif regtype+regid+'N' in semdict[tag]:
					genptr_src_read_new(f,regtype,regid)
			else:
				print "Bad register parse: ",regtype,regid,toss,numregs

## Generate the call to the helper
	f.write( "fWRAP_%s(" % tag)
	f.write( "SLOT_WRAP(gen_helper_%s(" % (tag))
	i=0
## If there is a scalar result, it is the return type
	for regtype,regid,toss,numregs in regs:
		if (regid[0] in "defgxyz"):
			# declaration
			if (len(regid) > 2):
				#print "error: put quads here"
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "V"):  # Quads for Splatter RF in Hana
						continue
			elif (len(regid) == 2):
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
						continue
					else:
						gen_helper_call_pair(f,regtype,regid,i)
				elif regtype+regid+'N' in semdict[tag]:
					gen_decl_pair_num(f,regtype,regid,i)
				# RsV0/RsV1 should not be supported any more...
				tmp = regid[0:len(regid)/2]
				for j in range(len(regid)):
					if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
						if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
							continue
						else:
							gen_helper_call(f,regtype,tmp,i,j)
			elif (len(regid) == 1):
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
						continue
					else:
						gen_helper_call(f,regtype,regid,i)
				elif regtype+regid+'N' in semdict[tag]:
					gen_helper_call(f,regtype,regid,i)
			else:
				print "Bad register parse: ",regtype,regid,toss,numregs
			i += 1
	if (i > 0): f.write( ", ")
	f.write( "cpu_env")
	i=1
	for regtype,regid,toss,numregs in regs:
		if (regid[0] in "defgxyz"):
			# declaration
			if (len(regid) > 2):
				#print "error: put quads here"
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "V"):  # Quads for Splatter RF in Hana
						gen_helper_call_ext_quad(f,regtype,regid,i)
			elif (len(regid) == 2):
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
						gen_helper_call_ext_pair(f,regtype,regid,i)
					else:
						if (regid[0] in "xyz"):
							continue
						else:
							continue
				elif regtype+regid+'N' in semdict[tag]:
					gen_decl_pair_num(f,regtype,regid,i)
				# RsV0/RsV1 should not be supported any more...
				tmp = regid[0:len(regid)/2]
				for j in range(len(regid)):
					if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
						if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
							gen_helper_call_ext(f,regtype,regid,i)
						else:
							continue
			elif (len(regid) == 1):
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
						gen_helper_call_ext(f,regtype,regid,i)
					else:
						continue
				elif regtype+regid+'N' in semdict[tag]:
					continue
			else:
				print "Bad register parse: ",regtype,regid,toss,numregs
			i += 1
	i=1
	for regtype,regid,toss,numregs in regs:
		if (regid[0] in "stuvwxyz"):
			# declaration
			if (len(regid) > 2):
				#print "error: put quads here"
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "V"):  # Quads for Splatter RF in Hana
						if (regid[0] in "xyz"):
							continue
						gen_helper_call_ext_quad(f,regtype,regid,i)
			elif (len(regid) == 2):
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
						if (regid[0] in "xyz"):
							continue
						gen_helper_call_ext_pair(f,regtype,regid,i)
					else:
						gen_helper_call_pair(f,regtype,regid,i)
				elif regtype+regid+'N' in semdict[tag]:
					gen_decl_pair_num(f,regtype,regid,i)
				# RsV0/RsV1 should not be supported any more...
				tmp = regid[0:len(regid)/2]
				for j in range(len(regid)):
					if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
						if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
							if (regid[0] in "xyz"):
								continue
							gen_helper_call_ext(f,regtype,regid,i)
						else:
							gen_helper_call(f,regtype,tmp,i,j)
			elif (len(regid) == 1):
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
						if (regid[0] in "xyz"):
							continue
						gen_helper_call_ext(f,regtype,regid,i)
					else:
						gen_helper_call(f,regtype,regid,i)
				elif regtype+regid+'N' in semdict[tag]:
					gen_helper_call_new(f,regtype,regid,i)
			else:
				print "Bad register parse: ",regtype,regid,toss,numregs
			i += 1
	for immlett,bits,immshift in imms:
		# declaration && READ
		if (immlett.isupper()):
			i = 1
		else:
			i = 0
		gen_helper_call_imm(f,'%siV' % immlett, i)
	if detectpart1.search(semdict[tag]): f.write( ", part1" )
	f.write( ", slot));,\n%s)\n" % semdict[tag] )

## Write all the outputs (destination regs)
	for regtype,regid,toss,numregs in regs:
		if (regid[0] in "defgxyz"):
			# destination or source/dest register
			if (len(regid) > 2):
				#print "error: put quads here"
				if regtype+regid+'V' in semdict[tag]:
					if (regtype in "V"): # Quads for Splatter RF in Hana
						genptr_dst_write_ext_quad(f,regtype,regid)
			elif (len(regid) == 2):
				if regtype+regid+'V' in semdict[tag]:
					#JPG: Write Pair for MMVECTOR
					if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
						if 'A_CVI_TMP' in attribdict[tag]:
							genptr_dst_write_ext_pair(f,regtype,regid,"","EXT_TMP")
						elif 'A_CVI_TMP_DST' in attribdict[tag]:
							genptr_dst_write_ext_pair(f,regtype,regid,"","EXT_TMP")
						else:
							genptr_dst_write_ext_pair(f,regtype,regid)
					else:
						genptr_dst_write_pair(f,regtype,regid)
				tmp = regid[0:len(regid)/2]
				for j in range(len(regid)):
					if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
						genptr_dst_write(f,regtype,tmp,j)
			elif (len(regid) == 1):
				if regtype+regid+'V' in semdict[tag]:
					# if dest is a vector check, call vector version
					if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
						# MMVEC if dest is a Vd.current or Vd.tmp load set the approriate flag for the log
						if 'A_CVI_NEW' in attribdict[tag]:
							genptr_dst_write_ext(f,regtype,regid,"","EXT_NEW")
						elif 'A_CVI_TMP' in attribdict[tag]:
							genptr_dst_write_ext(f,regtype,regid,"","EXT_TMP")
						elif 'A_CVI_TMP_DST' in attribdict[tag]:
							genptr_dst_write_ext(f,regtype,regid,"","EXT_TMP")
						else:
							genptr_dst_write_ext(f,regtype,regid,"","EXT_DFL")
					else:
						genptr_dst_write(f,regtype,regid)
			else:
				print "Bad register parse: ",regtype,regid,toss,numregs

	if 'A_FPOP' in attribdict[tag]:
		f.write('fFPOP_END();\n');


## Free all the operands (regs and immediates)
	if detectpart1.search(semdict[tag]): f.write( 'tcg_temp_free(part1);\n' )
	if detectea.search(semdict[tag]): gen_free_ea_tcg(f)
	for regtype,regid,toss,numregs in regs:
		# declaration
		if (len(regid) > 2):
			#print "error: put quads here"
			if regtype+regid+'V' in semdict[tag]:
				if (regtype in "V"):  # Quads for Splatter RF in Hana
					genptr_free_ext_quad(f,regtype,regid,i)
		elif (len(regid) == 2):
			if regtype+regid+'V' in semdict[tag]:
				if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
					genptr_free_ext_pair(f,regtype,regid,i)
				else:
					genptr_free_pair(f,regtype,regid,i)
			elif regtype+regid+'N' in semdict[tag]:
				gen_decl_pair_num(f,regtype,regid,i)
			# RsV0/RsV1 should not be supported any more...
			tmp = regid[0:len(regid)/2]
			for j in range(len(regid)):
				if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
					if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
						genptr_free_ext(f,regtype,regid,i)
					else:
						genptr_free(f,regtype,tmp,i,j)
		elif (len(regid) == 1):
			if regtype+regid+'V' in semdict[tag]:
				if (regtype in "VQZ"): # and ('A_CVI' in attribdict[tag]):
					genptr_free_ext(f,regtype,regid,i)
				else:
					genptr_free(f,regtype,regid,i)
			elif regtype+regid+'N' in semdict[tag]:
				genptr_free_new(f,regtype,regid,i)
		else:
			print "Bad register parse: ",regtype,regid,toss,numregs
		i += 1
	for immlett,bits,immshift in imms:
		# declaration && READ
		if (immlett.isupper()):
			i = 1
		else:
			i = 0
		genptr_free_imm(f,'%siV' % immlett, i)

	f.write( "/* %s */\n" % tag )
	f.write( "}" )
## End of the TCG generation function

## Start of the helper definition
	f.write( ",\n" )


	if (numscalarresults > 1):
## The helper is bogus when there is more than one result
		f.write( "void HELPER(%s)(CPUHexagonState *env) { BOGUS_HELPER(%s); }\n" % (tag, tag))
	else:
## The return type of the function is the type of the destination register (if scalar)
		i=0
		for regtype,regid,toss,numregs in regs:
			if (regid[0] in "defgxyz"):
				# destination or source/dest register
				if (len(regid) > 2):
					#print "error: put quads here"
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "V"): # Quads for Splatter RF in Hana
							continue
				elif (len(regid) == 2):
					if regtype+regid+'V' in semdict[tag]:
						#JPG: Write Pair for MMVECTOR
						if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
							if 'A_CVI_TMP' in attribdict[tag]:
								gen_dest_write_ext_pair(f,regtype,regid,"","EXT_TMP")
							elif 'A_CVI_TMP_DST' in attribdict[tag]:
								gen_dest_write_ext_pair(f,regtype,regid,"","EXT_TMP")
							else:
								continue
						else:
							gen_helper_return_type_pair(f,regtype,regid,i)
					tmp = regid[0:len(regid)/2]
					for j in range(len(regid)):
						if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
							gen_helper_return_type(f,regtype,tmp,i,j)
				elif (len(regid) == 1):
					if regtype+regid+'V' in semdict[tag]:
						# if dest is a vector check, call vector version
						if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
							# MMVEC if dest is a Vd.current or Vd.tmp load set the approriate flag for the log
							if 'A_CVI_NEW' in attribdict[tag]:
								continue
							elif 'A_CVI_TMP' in attribdict[tag]:
								continue
							elif 'A_CVI_TMP_DST' in attribdict[tag]:
								gen_dest_write_ext(f,regtype,regid,"","EXT_TMP")
							else:
								continue
						else:
							gen_helper_return_type(f,regtype,regid,i)
				else:
					print "Bad register parse: ",regtype,regid,toss,numregs
			i += 1

		if (numscalarresults == 0):
			f.write( "void" )
		f.write( " HELPER(%s)(CPUHexagonState *env, " % tag)
		i = 0
		for regtype,regid,toss,numregs in regs:
			if (regid[0] in "defgxyz"):
				# destination or source/dest register
				if (len(regid) > 2):
					#print "error: put quads here"
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "V"): # Quads for Splatter RF in Hana
							gen_helper_arg_ext_quad(f,regtype,regid,i)
				elif (len(regid) == 2):
					if regtype+regid+'V' in semdict[tag]:
						#JPG: Write Pair for MMVECTOR
						if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
							if 'A_CVI_TMP' in attribdict[tag]:
								gen_dest_write_ext_pair(f,regtype,regid,"","EXT_TMP")
							elif 'A_CVI_TMP_DST' in attribdict[tag]:
								gen_dest_write_ext_pair(f,regtype,regid,"","EXT_TMP")
							else:
								gen_helper_arg_ext_pair(f,regtype,regid,i)
						else:
							continue
					tmp = regid[0:len(regid)/2]
					for j in range(len(regid)):
						if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
							continue
				elif (len(regid) == 1):
					if regtype+regid+'V' in semdict[tag]:
						# if dest is a vector check, call vector version
						if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
							# MMVEC if dest is a Vd.current or Vd.tmp load set the approriate flag for the log
							if 'A_CVI_NEW' in attribdict[tag]:
								gen_helper_arg_ext(f,regtype,regid,i,"","EXT_NEW")
							elif 'A_CVI_TMP' in attribdict[tag]:
								gen_helper_arg_ext(f,regtype,regid,i,"","EXT_TMP")
							elif 'A_CVI_TMP_DST' in attribdict[tag]:
								gen_helper_arg_ext(f,regtype,regid,"","EXT_TMP")
							else:
								gen_helper_arg_ext(f,regtype,regid,i,"","EXT_DFL")
						else:
							# This is the return value of the function
							continue
				else:
					print "Bad register parse: ",regtype,regid,toss,numregs
				i += 1

## Arguments to the helper function are the source regs and immediates
		for regtype,regid,toss,numregs in regs:
			if (regid[0] in "stuvwxyz"):
				# source or source/dest register
				if (len(regid) > 2):
					#print "error: put quads here"
					if regtype+regid+'V' in semdict[tag]: # Quads for Splatter RF in Hana
						if (regtype in "V"):
							if 'A_CVI_TMP_SRC' in attribdict[tag]:
								if (regid[0] in "xyz"):
									continue
								gen_helper_arg_ext_quad(f,regtype,regid,i,"","EXT_TMP")
							else:
								if (regid[0] in "xyz"):
									continue
								gen_helper_arg_ext_quad(f,regtype,regid,i)
				elif (len(regid) == 2):
					if regtype+regid+'V' in semdict[tag]:
						#JPG: Crazy case of slot 3 to slot 2 forwarding for silver
						if (regtype in "VZ") and (regid[0] in "x") and ('A_VECX_ACCFWD' in attribdict[tag]):
							gen_source_read_ext_acc_pair(f,regtype,regid)
						elif (regtype in "VZQ"): # and ('A_CVI' in attribdict[tag]):
							if 'A_CVI_TMP_SRC' in attribdict[tag]:
								if (regid[0] in "xyz"):
									continue
								gen_helper_arg_ext_pair(f,regtype,regid,i,"","EXT_TMP")
							else:
								if (regid[0] in "xyz"):
									continue
								gen_helper_arg_ext_pair(f,regtype,regid,i)
						else:
							gen_helper_arg_pair(f,regtype,regid,i)
					tmp = regid[0:len(regid)/2]
					for j in range(len(regid)):
						if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
							gen_helper_arg(f,regtype,tmp,i,j)
				elif (len(regid) == 1):
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "VZ") and (regid[0] in "x") and ('A_VECX_ACCFWD' in attribdict[tag]):
							gen_source_read_ext_acc(f,regtype,regid)
						# JPG: if source is a vector check, call vector version
						elif (regtype in "VZQ"): # and ('A_CVI' in attribdict[tag]):
							if 'A_CVI_TMP_SRC' in attribdict[tag]:
								if (regid[0] in "xyz"):
									continue
								gen_helper_arg_ext(f,regtype,regid,i,"","EXT_TMP")
							else:
								if (regid[0] in "xyz"):
									continue
								gen_helper_arg_ext(f,regtype,regid,i)
						else:
							gen_helper_arg(f,regtype,regid,i)
					elif regtype+regid+'N' in semdict[tag]:
						gen_helper_arg_new(f,regtype,regid,i)
				else:
					print "Bad register parse: ",regtype,regid,toss,numregs

				i += 1
		for immlett,bits,immshift in imms:
			# declaration && READ
			if (immlett.isupper()):
				j = 1
			else:
				j = 0
			gen_helper_arg_imm(f,'%siV' % immlett, j, i)
			i += 1
		if detectpart1.search(semdict[tag]): f.write( ', uint32_t part1' )
		if i > 0: f.write( ", " )
		f.write( "uint32_t slot)\n{\n" )
## Declare the return variable
		if detectea.search(semdict[tag]): gen_decl_ea(f)
		i=0
		for regtype,regid,toss,numregs in regs:
			if (regid[0] in "defg"):
				# destination register
				if (len(regid) > 2):
					#print "error: put quads here"
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "V"): # Quads for Splatter RF in Hana
							gen_helper_dest_decl_ext_quad(f,regtype,regid)
				elif (len(regid) == 2):
					if regtype+regid+'V' in semdict[tag]:
						#JPG: Write Pair for MMVECTOR
						if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
							if 'A_CVI_TMP' in attribdict[tag]:
								gen_dest_write_ext_pair(f,regtype,regid,"","EXT_TMP")
							elif 'A_CVI_TMP_DST' in attribdict[tag]:
								gen_dest_write_ext_pair(f,regtype,regid,"","EXT_TMP")
							else:
								gen_helper_dest_decl_ext_pair(f,regtype,regid, i)
						else:
							gen_helper_dest_decl_pair(f,regtype,regid,i)
					tmp = regid[0:len(regid)/2]
					for j in range(len(regid)):
						if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
							gen_helper_dest_decl(f,regtype,tmp,i,j)
				elif (len(regid) == 1):
					if regtype+regid+'V' in semdict[tag]:
						# if dest is a vector check, call vector version
						if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
							# MMVEC if dest is a Vd.current or Vd.tmp load set the approriate flag for the log
							if 'A_CVI_NEW' in attribdict[tag]:
								gen_helper_dest_decl_ext(f,regtype,regid,"","EXT_NEW")
							elif 'A_CVI_TMP' in attribdict[tag]:
								gen_helper_dest_decl_ext(f,regtype,regid,"","EXT_TMP")
							elif 'A_CVI_TMP_DST' in attribdict[tag]:
								f.write("YO3")
								gen_dest_write_ext(f,regtype,regid,"","EXT_TMP")
							else:
								gen_helper_dest_decl_ext(f,regtype,regid,"","EXT_DFL")
						else:
							gen_helper_dest_decl(f,regtype,regid,i)
				else:
					print "Bad register parse: ",regtype,regid,toss,numregs
			i += 1
		for regtype,regid,toss,numregs in regs:
			if (regid[0] in "stuvwxyz"):
				# source or source/dest register
				if (len(regid) > 2):
					#print "error: put quads here"
					if regtype+regid+'V' in semdict[tag]: # Quads for Splatter RF in Hana
						if (regtype in "V"):
							if 'A_CVI_TMP_SRC' in attribdict[tag]:
								gen_helper_src_var_ext_quad(f,regtype,regid,"","EXT_TMP")
							else:
								gen_helper_src_var_ext_quad(f,regtype,regid)
				elif (len(regid) == 2):
					if regtype+regid+'V' in semdict[tag]:
						#JPG: Crazy case of slot 3 to slot 2 forwarding for silver
						if (regtype in "VZ") and (regid[0] in "x") and ('A_VECX_ACCFWD' in attribdict[tag]):
							gen_source_read_ext_acc_pair(f,regtype,regid)
						else:
							gen_helper_src_var_ext_pair(f,regtype,regid,i)
					tmp = regid[0:len(regid)/2]
					for j in range(len(regid)):
						if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
							gen_helper_arg(f,regtype,tmp,i,j)
				elif (len(regid) == 1):
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "VZ") and (regid[0] in "x") and ('A_VECX_ACCFWD' in attribdict[tag]):
							gen_source_read_ext_acc(f,regtype,regid)
						# JPG: if source is a vector check, call vector version
						elif (regtype in "VZQ"): # and ('A_CVI' in attribdict[tag]):
							if 'A_CVI_TMP_SRC' in attribdict[tag]:
								gen_helper_src_var_ext(f,regtype,regid,"","EXT_TMP")
							else:
								gen_helper_src_var_ext(f,regtype,regid,"","EXT_TMP")
				else:
					print "Bad register parse: ",regtype,regid,toss,numregs

		if 'A_FPOP' in attribdict[tag]:
			f.write('fFPOP_START();\n');
		f.write( semdict[tag] )
		f.write( "\n" )
		if 'A_FPOP' in attribdict[tag]:
			f.write('fFPOP_END();\n');
## Save the return variable
		i=0
		for regtype,regid,toss,numregs in regs:
			if (regid[0] in "defgxyz"):
				# destination or source/dest register
				if (len(regid) > 2):
					#print "error: put quads here"
					if regtype+regid+'V' in semdict[tag]:
						if (regtype in "V"): # Quads for Splatter RF in Hana
							gen_helper_dst_write_ext_quad(f,regtype,regid)
				elif (len(regid) == 2):
					if regtype+regid+'V' in semdict[tag]:
						#JPG: Write Pair for MMVECTOR
						if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
							if 'A_CVI_TMP' in attribdict[tag]:
								gen_dest_write_ext_pair(f,regtype,regid,"","EXT_TMP")
							elif 'A_CVI_TMP_DST' in attribdict[tag]:
								gen_dest_write_ext_pair(f,regtype,regid,"","EXT_TMP")
							else:
								gen_helper_dst_write_ext_pair(f,regtype,regid)
						else:
							gen_helper_return_pair(f,regtype,regid,i)
					tmp = regid[0:len(regid)/2]
					for j in range(len(regid)):
						if ('%s%sV%d' % (regtype,tmp,j)) in semdict[tag]:
							gen_helper_return(f,regtype,tmp,i,j)
				elif (len(regid) == 1):
					if regtype+regid+'V' in semdict[tag]:
						# if dest is a vector check, call vector version
						if (regtype in "ZVQ"): # and ('A_CVI' in attribdict[tag]):
							# MMVEC if dest is a Vd.current or Vd.tmp load set the approriate flag for the log
							if 'A_CVI_NEW' in attribdict[tag]:
								gen_helper_dst_write_ext(f,regtype,regid,"","EXT_NEW")
							elif 'A_CVI_TMP' in attribdict[tag]:
								gen_helper_dst_write_ext(f,regtype,regid,"","EXT_TMP")
							elif 'A_CVI_TMP_DST' in attribdict[tag]:
								gen_helper_dst_write_ext(f,regtype,regid,"","EXT_TMP")
							else:
								gen_helper_dst_write_ext(f,regtype,regid,"","EXT_DFL")
						else:
							gen_helper_return(f,regtype,regid,i)
				else:
					print "Bad register parse: ",regtype,regid,toss,numregs
			i += 1
		f.write( "}" )
## End of the helper definition
	f.write( ")\n" )

wrap_hdr = open ('qemu_wrap.h', 'w')
for tag in tags:
	wrap_hdr.write( "#ifndef fWRAP_%s\n" % tag )
	wrap_hdr.write( "#define fWRAP_%s(GENHLPR, SHORTCODE) GENHLPR\n" % tag )
	wrap_hdr.write( "#endif\n\n" )
wrap_hdr.close()

f = cStringIO.StringIO()

f.write("""
#ifndef DEF_QEMU
#define DEF_QEMU(TAG,SHORTCODE,HELPER,GENFN,HELPFN)   /* Nothing */
#endif

""")


for tag in tags:
	ext = ""
	if extdict[tag]:
		ext = "_EXT_%s" % extdict[tag]
		if "A_EXTENSION" not in attribdict[tag]: attribdict[tag].add("A_EXTENSION")
## Skip assembler mapped instructions
	if "A_MAPPING" in attribdict[tag]:
		continue
	if ( tag == "V6_pz" ) :
		continue
## Skip the print vector instructions
	if ( tag == "V6_pv32" ) :
		continue
	if ( tag == "V6_pv32d" ) :
		continue
	if ( tag == "V6_pv32du" ) :
		continue
	if ( tag == "V6_pv64d" ) :
		continue
	if ( tag == "V6_pv16" ) :
		continue
	if ( tag == "V6_pv16d" ) :
		continue
	if ( tag == "V6_pv8d" ) :
		continue
	if ( tag == "V6_pv8" ) :
		continue
	if ( tag == "V6_preg" ) :
		continue
	if ( tag == "V6_pregd" ) :
		continue
	if ( tag == "V6_pregf" ) :
		continue
	if ( tag == "V6_ppred" ) :
		continue
	if ( ext == "_EXT_mmvec" ) :
		gen_qemu_hvx(f, tag)
		continue
## Skip the user defined instruction
	if ( tag == "S6_userinsn" ) :
		continue
## Skip the diag instructions
	if ( tag == "Y6_diag" ) :
		continue
	if ( tag == "Y6_diag0" ) :
		continue
	if ( tag == "Y6_diag1" ) :
		continue
## Skip the priv instructions
	if ( "A_PRIV" in attribdict[tag] ) :
		continue
## Skip the guest instructions
	if ( "A_GUEST" in attribdict[tag] ) :
		continue
## Skip the noext instructions FIXME
	if ( ext == "_EXT_noext" ) :
		continue

	gen_qemu_scalar(f, tag)

realf = open('qemu.odef','w')
realf.write(f.getvalue())
realf.close()
f.close()

