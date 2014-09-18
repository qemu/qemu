#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Type-transformation rules.
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2014, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@linux.vnet.ibm.com"


def _transform_type(type_, trans):
    if isinstance(trans, str):
        return trans
    elif isinstance(trans, dict):
        if type_ in trans:
            return _transform_type(type_, trans[type_])
        elif None in trans:
            return _transform_type(type_, trans[None])
        else:
            return type_
    elif callable(trans):
        return trans(type_)
    else:
        raise ValueError("Invalid type transformation rule: %s" % trans)


def transform_type(type_, *trans):
    """Return a new type transformed according to the given rules.

    Applies each of the transformation rules in trans in order.

    If an element of trans is a string, return it.

    If an element of trans is a function, call it with type_ as its only
    argument.

    If an element of trans is a dict, search type_ in its keys. If type_ is
    a key, use the value as a transformation rule for type_. Otherwise, if
    None is a key use the value as a transformation rule for type_.

    Otherwise, return type_.

    Parameters
    ----------
    type_ : str
        Type to transform.
    trans : list of function or dict
        Type transformation rules.
    """
    if len(trans) == 0:
        raise ValueError
    res = type_
    for t in trans:
        res = _transform_type(res, t)
    return res


##################################################
# tcg -> host

def _tcg_2_host(type_):
    if type_ == "TCGv":
        # force a fixed-size type (target-independent)
        return "uint64_t"
    else:
        return type_

TCG_2_HOST = {
    "TCGv_i32": "uint32_t",
    "TCGv_i64": "uint64_t",
    "TCGv_ptr": "void *",
    None: _tcg_2_host,
    }


##################################################
# host -> host compatible with tcg sizes

HOST_2_TCG_COMPAT = {
    "uint8_t": "uint32_t",
    }


##################################################
# host/tcg -> tcg

def _host_2_tcg(type_):
    if type_.startswith("TCGv"):
        return type_
    raise ValueError("Don't know how to translate '%s' into a TCG type\n" % type_)

HOST_2_TCG = {
    "uint32_t": "TCGv_i32",
    "uint64_t": "TCGv_i64",
    "void *"  : "TCGv_ptr",
    None: _host_2_tcg,
    }


##################################################
# tcg -> tcg helper definition

def _tcg_2_helper_def(type_):
    if type_ == "TCGv":
        return "target_ulong"
    else:
        return type_

TCG_2_TCG_HELPER_DEF = {
    "TCGv_i32": "uint32_t",
    "TCGv_i64": "uint64_t",
    "TCGv_ptr": "void *",
    None: _tcg_2_helper_def,
    }


##################################################
# tcg -> tcg helper declaration

def _tcg_2_tcg_helper_decl_error(type_):
    raise ValueError("Don't know how to translate type '%s' into a TCG helper declaration type\n" % type_)

TCG_2_TCG_HELPER_DECL = {
    "TCGv"    : "tl",
    "TCGv_ptr": "ptr",
    "TCGv_i32": "i32",
    "TCGv_i64": "i64",
    None: _tcg_2_tcg_helper_decl_error,
    }


##################################################
# host/tcg -> tcg temporal constant allocation

def _host_2_tcg_tmp_new(type_):
    if type_.startswith("TCGv"):
        return "tcg_temp_new_nop"
    raise ValueError("Don't know how to translate type '%s' into a TCG temporal allocation" % type_)

HOST_2_TCG_TMP_NEW = {
    "uint32_t": "tcg_const_i32",
    "uint64_t": "tcg_const_i64",
    "void *"  : "tcg_const_ptr",
    None: _host_2_tcg_tmp_new,
    }


##################################################
# host/tcg -> tcg temporal constant deallocation

def _host_2_tcg_tmp_free(type_):
    if type_.startswith("TCGv"):
        return "tcg_temp_free_nop"
    raise ValueError("Don't know how to translate type '%s' into a TCG temporal deallocation" % type_)

HOST_2_TCG_TMP_FREE = {
    "uint32_t": "tcg_temp_free_i32",
    "uint64_t": "tcg_temp_free_i64",
    "void *"  : "tcg_temp_free_ptr",
    None: _host_2_tcg_tmp_free,
    }
