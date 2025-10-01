# SPDX-License-Identifier: GPL-2.0-or-later

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

=========================== ====================================================
Attribute                   Description
=========================== ====================================================
PUBLIC                      If exists and is set to 'True', the backend is
                            considered "public".
CHECK_TRACE_EVENT_GET_STATE If exists and is set to 'True', the backend-specific
                            code inside the tracepoint is emitted within an
                            ``if trace_event_get_state()`` conditional.
=========================== ====================================================


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
__email__      = "stefanha@redhat.com"


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
    return tracetool.try_import("tracetool.backend." + name)[0]


class Wrapper:
    def __init__(self, backends, format):
        self._backends = [backend.replace("-", "_") for backend in backends]
        self._format = format.replace("-", "_")
        self.check_trace_event_get_state = False
        for backend in self._backends:
            assert exists(backend)
        assert tracetool.format.exists(self._format)
        for backend in self.backend_modules():
            check_trace_event_get_state = getattr(backend, "CHECK_TRACE_EVENT_GET_STATE", False)
            self.check_trace_event_get_state = self.check_trace_event_get_state or check_trace_event_get_state

    def backend_modules(self):
        for backend in self._backends:
             module = tracetool.try_import("tracetool.backend." + backend)[1]
             if module is not None:
                 yield module

    def _run_function(self, name, *args, check_trace_event_get_state=None, **kwargs):
        for backend in self.backend_modules():
            func = getattr(backend, name % self._format, None)
            if func is not None and \
                (check_trace_event_get_state is None or
                 check_trace_event_get_state == getattr(backend, 'CHECK_TRACE_EVENT_GET_STATE', False)):
                    func(*args, **kwargs)

    def generate_begin(self, events, group):
        self._run_function("generate_%s_begin", events, group)

    def generate(self, event, group, check_trace_event_get_state=None):
        self._run_function("generate_%s", event, group, check_trace_event_get_state=check_trace_event_get_state)

    def generate_backend_dstate(self, event, group):
        self._run_function("generate_%s_backend_dstate", event, group)

    def generate_end(self, events, group):
        self._run_function("generate_%s_end", events, group)
