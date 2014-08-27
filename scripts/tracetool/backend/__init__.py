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

All the following functions are optional, and no output will be generated if
they do not exist.

=============================== ==============================================
Function                        Description
=============================== ==============================================
generate_<format>_begin(events) Generate backend- and format-specific file
                                header contents.
generate_<format>_end(events)   Generate backend- and format-specific file
                                footer contents.
generate_<format>(event)        Generate backend- and format-specific contents
                                for the given event.
=============================== ==============================================

"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2012-2014, Lluís Vilanova <vilanova@ac.upc.edu>"
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


class Wrapper:
    def __init__(self, backends, format):
        self._backends = [backend.replace("-", "_") for backend in backends]
        self._format = format.replace("-", "_")
        for backend in self._backends:
            assert exists(backend)
        assert tracetool.format.exists(self._format)

    def _run_function(self, name, *args, **kwargs):
        for backend in self._backends:
            func = tracetool.try_import("tracetool.backend." + backend,
                                        name % self._format, None)[1]
            if func is not None:
                func(*args, **kwargs)

    def generate_begin(self, events):
        self._run_function("generate_%s_begin", events)

    def generate(self, event):
        self._run_function("generate_%s", event)

    def generate_end(self, events):
        self._run_function("generate_%s_end", events)
