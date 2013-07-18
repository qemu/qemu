#
# QAPI command marshaller generator
#
# Copyright IBM, Corp. 2011
#
# Authors:
#  Anthony Liguori <aliguori@us.ibm.com>
#  Michael Roth    <mdroth@linux.vnet.ibm.com>
#
# This work is licensed under the terms of the GNU GPLv2.
# See the COPYING.LIB file in the top-level directory.

from ordereddict import OrderedDict
from qapi import *
import sys
import os
import getopt
import errno

def type_visitor(name):
    if type(name) == list:
        return 'visit_type_%sList' % name[0]
    else:
        return 'visit_type_%s' % name

def generate_decl_enum(name, members, genlist=True):
    return mcgen('''

void %(visitor)s(Visitor *m, %(name)s * obj, const char *name, Error **errp);
''',
                 visitor=type_visitor(name))

def generate_command_decl(name, args, ret_type):
    arglist=""
    for argname, argtype, optional, structured in parse_args(args):
        argtype = c_type(argtype)
        if argtype == "char *":
            argtype = "const char *"
        if optional:
            arglist += "bool has_%s, " % c_var(argname)
        arglist += "%s %s, " % (argtype, c_var(argname))
    return mcgen('''
%(ret_type)s qmp_%(name)s(%(args)sError **errp);
''',
                 ret_type=c_type(ret_type), name=c_fun(name), args=arglist).strip()

def gen_sync_call(name, args, ret_type, indent=0):
    ret = ""
    arglist=""
    retval=""
    if ret_type:
        retval = "retval = "
    for argname, argtype, optional, structured in parse_args(args):
        if optional:
            arglist += "has_%s, " % c_var(argname)
        arglist += "%s, " % (c_var(argname))
    push_indent(indent)
    ret = mcgen('''
%(retval)sqmp_%(name)s(%(args)serrp);

''',
                name=c_fun(name), args=arglist, retval=retval).rstrip()
    if ret_type:
        ret += "\n" + mcgen(''''
if (!error_is_set(errp)) {
    %(marshal_output_call)s
}
''',
                            marshal_output_call=gen_marshal_output_call(name, ret_type)).rstrip()
    pop_indent(indent)
    return ret.rstrip()


def gen_marshal_output_call(name, ret_type):
    if not ret_type:
        return ""
    return "qmp_marshal_output_%s(retval, ret, errp);" % c_fun(name)

def gen_visitor_output_containers_decl(ret_type):
    ret = ""
    push_indent()
    if ret_type:
        ret += mcgen('''
QmpOutputVisitor *mo;
QapiDeallocVisitor *md;
Visitor *v;
''')
    pop_indent()

    return ret

def gen_visitor_input_containers_decl(args):
    ret = ""

    push_indent()
    if len(args) > 0:
        ret += mcgen('''
QmpInputVisitor *mi;
QapiDeallocVisitor *md;
Visitor *v;
''')
    pop_indent()

    return ret.rstrip()

def gen_visitor_input_vars_decl(args):
    ret = ""
    push_indent()
    for argname, argtype, optional, structured in parse_args(args):
        if optional:
            ret += mcgen('''
bool has_%(argname)s = false;
''',
                         argname=c_var(argname))
        if c_type(argtype).endswith("*"):
            ret += mcgen('''
%(argtype)s %(argname)s = NULL;
''',
                         argname=c_var(argname), argtype=c_type(argtype))
        else:
            ret += mcgen('''
%(argtype)s %(argname)s;
''',
                         argname=c_var(argname), argtype=c_type(argtype))

    pop_indent()
    return ret.rstrip()

def gen_visitor_input_block(args, obj, dealloc=False):
    ret = ""
    errparg = 'errp'

    if len(args) == 0:
        return ret

    push_indent()

    if dealloc:
        errparg = 'NULL'
        ret += mcgen('''
md = qapi_dealloc_visitor_new();
v = qapi_dealloc_get_visitor(md);
''')
    else:
        ret += mcgen('''
mi = qmp_input_visitor_new_strict(%(obj)s);
v = qmp_input_get_visitor(mi);
''',
                     obj=obj)

    for argname, argtype, optional, structured in parse_args(args):
        if optional:
            ret += mcgen('''
visit_start_optional(v, &has_%(c_name)s, "%(name)s", %(errp)s);
if (has_%(c_name)s) {
''',
                         c_name=c_var(argname), name=argname, errp=errparg)
            push_indent()
        ret += mcgen('''
%(visitor)s(v, &%(c_name)s, "%(name)s", %(errp)s);
''',
                     c_name=c_var(argname), name=argname, argtype=argtype,
                     visitor=type_visitor(argtype), errp=errparg)
        if optional:
            pop_indent()
            ret += mcgen('''
}
visit_end_optional(v, %(errp)s);
''', errp=errparg)

    if dealloc:
        ret += mcgen('''
qapi_dealloc_visitor_cleanup(md);
''')
    else:
        ret += mcgen('''
qmp_input_visitor_cleanup(mi);
''')
    pop_indent()
    return ret.rstrip()

def gen_marshal_output(name, args, ret_type, middle_mode):
    if not ret_type:
        return ""

    ret = mcgen('''
static void qmp_marshal_output_%(c_name)s(%(c_ret_type)s ret_in, QObject **ret_out, Error **errp)
{
    QapiDeallocVisitor *md = qapi_dealloc_visitor_new();
    QmpOutputVisitor *mo = qmp_output_visitor_new();
    Visitor *v;

    v = qmp_output_get_visitor(mo);
    %(visitor)s(v, &ret_in, "unused", errp);
    if (!error_is_set(errp)) {
        *ret_out = qmp_output_get_qobject(mo);
    }
    qmp_output_visitor_cleanup(mo);
    v = qapi_dealloc_get_visitor(md);
    %(visitor)s(v, &ret_in, "unused", NULL);
    qapi_dealloc_visitor_cleanup(md);
}
''',
                c_ret_type=c_type(ret_type), c_name=c_fun(name),
                visitor=type_visitor(ret_type))

    return ret

def gen_marshal_input_decl(name, args, ret_type, middle_mode):
    if middle_mode:
        return 'int qmp_marshal_input_%s(Monitor *mon, const QDict *qdict, QObject **ret)' % c_fun(name)
    else:
        return 'static void qmp_marshal_input_%s(QDict *args, QObject **ret, Error **errp)' % c_fun(name)



def gen_marshal_input(name, args, ret_type, middle_mode):
    hdr = gen_marshal_input_decl(name, args, ret_type, middle_mode)

    ret = mcgen('''
%(header)s
{
''',
                header=hdr)

    if middle_mode:
        ret += mcgen('''
    Error *local_err = NULL;
    Error **errp = &local_err;
    QDict *args = (QDict *)qdict;
''')

    if ret_type:
        if c_type(ret_type).endswith("*"):
            retval = "    %s retval = NULL;" % c_type(ret_type)
        else:
            retval = "    %s retval;" % c_type(ret_type)
        ret += mcgen('''
%(retval)s
''',
                     retval=retval)

    if len(args) > 0:
        ret += mcgen('''
%(visitor_input_containers_decl)s
%(visitor_input_vars_decl)s

%(visitor_input_block)s

''',
                     visitor_input_containers_decl=gen_visitor_input_containers_decl(args),
                     visitor_input_vars_decl=gen_visitor_input_vars_decl(args),
                     visitor_input_block=gen_visitor_input_block(args, "QOBJECT(args)"))
    else:
        ret += mcgen('''
    (void)args;
''')

    ret += mcgen('''
    if (error_is_set(errp)) {
        goto out;
    }
%(sync_call)s
''',
                 sync_call=gen_sync_call(name, args, ret_type, indent=4))
    ret += mcgen('''

out:
''')
    ret += mcgen('''
%(visitor_input_block_cleanup)s
''',
                 visitor_input_block_cleanup=gen_visitor_input_block(args, None,
                                                                     dealloc=True))

    if middle_mode:
        ret += mcgen('''

    if (local_err) {
        qerror_report_err(local_err);
        error_free(local_err);
        return -1;
    }
    return 0;
''')
    else:
        ret += mcgen('''
    return;
''')

    ret += mcgen('''
}
''')

    return ret

def option_value_matches(opt, val, cmd):
    if opt in cmd and cmd[opt] == val:
        return True
    return False

def gen_registry(commands):
    registry=""
    push_indent()
    for cmd in commands:
        options = 'QCO_NO_OPTIONS'
        if option_value_matches('success-response', 'no', cmd):
            options = 'QCO_NO_SUCCESS_RESP'

        registry += mcgen('''
qmp_register_command("%(name)s", qmp_marshal_input_%(c_name)s, %(opts)s);
''',
                     name=cmd['command'], c_name=c_fun(cmd['command']),
                     opts=options)
    pop_indent()
    ret = mcgen('''
static void qmp_init_marshal(void)
{
%(registry)s
}

qapi_init(qmp_init_marshal);
''',
                registry=registry.rstrip())
    return ret

def gen_command_decl_prologue(header, guard, prefix=""):
    ret = mcgen('''
/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT MODIFY */

/*
 * schema-defined QAPI function prototypes
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef %(guard)s
#define %(guard)s

#include "%(prefix)sqapi-types.h"
#include "qapi/qmp/qdict.h"
#include "qapi/error.h"

''',
                 header=basename(header), guard=guardname(header), prefix=prefix)
    return ret

def gen_command_def_prologue(prefix="", proxy=False):
    ret = mcgen('''
/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT MODIFY */

/*
 * schema-defined QMP->QAPI command dispatch
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "qemu/module.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/types.h"
#include "qapi/qmp/dispatch.h"
#include "qapi/visitor.h"
#include "qapi/qmp-output-visitor.h"
#include "qapi/qmp-input-visitor.h"
#include "qapi/dealloc-visitor.h"
#include "%(prefix)sqapi-types.h"
#include "%(prefix)sqapi-visit.h"

''',
                prefix=prefix)
    if not proxy:
        ret += '#include "%sqmp-commands.h"' % prefix
    return ret + "\n\n"


try:
    opts, args = getopt.gnu_getopt(sys.argv[1:], "chp:o:m",
                                   ["source", "header", "prefix=",
                                    "output-dir=", "type=", "middle"])
except getopt.GetoptError, err:
    print str(err)
    sys.exit(1)

output_dir = ""
prefix = ""
dispatch_type = "sync"
c_file = 'qmp-marshal.c'
h_file = 'qmp-commands.h'
middle_mode = False

do_c = False
do_h = False

for o, a in opts:
    if o in ("-p", "--prefix"):
        prefix = a
    elif o in ("-o", "--output-dir"):
        output_dir = a + "/"
    elif o in ("-t", "--type"):
        dispatch_type = a
    elif o in ("-m", "--middle"):
        middle_mode = True
    elif o in ("-c", "--source"):
        do_c = True
    elif o in ("-h", "--header"):
        do_h = True

if not do_c and not do_h:
    do_c = True
    do_h = True

c_file = output_dir + prefix + c_file
h_file = output_dir + prefix + h_file

def maybe_open(really, name, opt):
    if really:
        return open(name, opt)
    else:
        import StringIO
        return StringIO.StringIO()

try:
    os.makedirs(output_dir)
except os.error, e:
    if e.errno != errno.EEXIST:
        raise

exprs = parse_schema(sys.stdin)
commands = filter(lambda expr: expr.has_key('command'), exprs)
commands = filter(lambda expr: not expr.has_key('gen'), commands)

if dispatch_type == "sync":
    fdecl = maybe_open(do_h, h_file, 'w')
    fdef = maybe_open(do_c, c_file, 'w')
    ret = gen_command_decl_prologue(header=basename(h_file), guard=guardname(h_file), prefix=prefix)
    fdecl.write(ret)
    ret = gen_command_def_prologue(prefix=prefix)
    fdef.write(ret)

    for cmd in commands:
        arglist = []
        ret_type = None
        if cmd.has_key('data'):
            arglist = cmd['data']
        if cmd.has_key('returns'):
            ret_type = cmd['returns']
        ret = generate_command_decl(cmd['command'], arglist, ret_type) + "\n"
        fdecl.write(ret)
        if ret_type:
            ret = gen_marshal_output(cmd['command'], arglist, ret_type, middle_mode) + "\n"
            fdef.write(ret)

        if middle_mode:
            fdecl.write('%s;\n' % gen_marshal_input_decl(cmd['command'], arglist, ret_type, middle_mode))

        ret = gen_marshal_input(cmd['command'], arglist, ret_type, middle_mode) + "\n"
        fdef.write(ret)

    fdecl.write("\n#endif\n");

    if not middle_mode:
        ret = gen_registry(commands)
        fdef.write(ret)

    fdef.flush()
    fdef.close()
    fdecl.flush()
    fdecl.close()
