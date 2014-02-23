#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Backend management.


Creating new backends
---------------------

A new backend named 'foo-bar' corresponds to Python module
'tracetool/backend/foo_bar.py'.

A backend module should provide a docstring, whose first non-empty line will be
considered its short description.

All backends must generate their contents through the 'tracetool.out' routine.


Backend attributes
------------------

========= ====================================================================
Attribute Description
========= ====================================================================
PUBLIC    If exists and is set to 'True', the backend is considered "public".
========= ====================================================================


Backend functions
-----------------

======== =======================================================================
Function Description
======== =======================================================================
<format> Called to generate the format- and backend-specific code for each of
         the specified events. If the function does not exist, the backend is
         considered not compatible with the given format.
======== =======================================================================
"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@linux.vnet.ibm.com"


import os

import tracetool


def get_list(only_public = False):
    """Get a list of (name, description) pairs."""
    res = [("nop", "Tracing disabled.")]
    modnames = []
    for filename in os.listdir(tracetool.backend.__path__[0]):
        if filename.endswith('.py') and filename != '__init__.py':
            modnames.append(filename.rsplit('.', 1)[0])
    for modname in sorted(modnames):
        module = tracetool.try_import("tracetool.backend." + modname)

        # just in case; should never fail unless non-module files are put there
        if not module[0]:
            continue
        module = module[1]

        public = getattr(module, "PUBLIC", False)
        if only_public and not public:
            continue

        doc = module.__doc__
        if doc is None:
            doc = ""
        doc = doc.strip().split("\n")[0]

        name = modname.replace("_", "-")
        res.append((name, doc))
    return res


def exists(name):
    """Return whether the given backend exists."""
    if len(name) == 0:
        return False
    if name == "nop":
        return True
    name = name.replace("-", "_")
    return tracetool.try_import("tracetool.backend." + name)[1]


def compatible(backend, format):
    """Whether a backend is compatible with the given format."""
    if not exists(backend):
        raise ValueError("unknown backend: %s" % backend)

    backend = backend.replace("-", "_")
    format = format.replace("-", "_")

    if backend == "nop":
        return True
    else:
        func = tracetool.try_import("tracetool.backend." + backend,
                                    format, None)[1]
        return func is not None


def _empty(events):
    pass

def generate(backend, format, events):
    """Generate the per-event output for the given (backend, format) pair."""
    if not compatible(backend, format):
        raise ValueError("backend '%s' not compatible with format '%s'" %
                         (backend, format))

    backend = backend.replace("-", "_")
    format = format.replace("-", "_")

    if backend == "nop":
        func = tracetool.try_import("tracetool.format." + format,
                                    "nop", _empty)[1]
    else:
        func = tracetool.try_import("tracetool.backend." + backend,
                                    format, None)[1]

    func(events)
