# -*- coding: utf-8 -*-

"""
Generic management for the 'vcpu' property.

"""

__author__     = "Lluís Vilanova <vilanova@ac.upc.edu>"
__copyright__  = "Copyright 2016, Lluís Vilanova <vilanova@ac.upc.edu>"
__license__    = "GPL version 2 or (at your option) any later version"

__maintainer__ = "Stefan Hajnoczi"
__email__      = "stefanha@redhat.com"


from tracetool import Arguments, try_import


def transform_event(event):
    """Transform event to comply with the 'vcpu' property (if present)."""
    if "vcpu" in event.properties:
        event.args = Arguments([("void *", "__cpu"), event.args])
        fmt = "\"cpu=%p \""
        event.fmt = fmt + event.fmt
    return event


def transform_args(format, event, *args, **kwargs):
    """Transforms the arguments to suit the specified format.

    The format module must implement function 'vcpu_args', which receives the
    implicit arguments added by the 'vcpu' property, and must return suitable
    arguments for the given format.

    The function is only called for events with the 'vcpu' property.

    Parameters
    ==========
    format : str
        Format module name.
    event : Event
    args, kwargs
        Passed to 'vcpu_transform_args'.

    Returns
    =======
    Arguments
        The transformed arguments, including the non-implicit ones.

    """
    if "vcpu" in event.properties:
        ok, func = try_import("tracetool.format." + format,
                              "vcpu_transform_args")
        assert ok
        assert func
        return Arguments([func(event.args[:1], *args, **kwargs),
                          event.args[1:]])
    else:
        return event.args
