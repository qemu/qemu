
Specify tracing options.

``[enable=]PATTERN``

  Immediately enable events matching *PATTERN*
  (either event name or a globbing pattern).  This option is only
  available if QEMU has been compiled with the ``simple``, ``log``
  or ``ftrace`` tracing backend.  To specify multiple events or patterns,
  specify the :option:`-trace` option multiple times.

  Use :option:`-trace help` to print a list of names of trace points.

``events=FILE``

  Immediately enable events listed in *FILE*.
  The file must contain one event name (as listed in the ``trace-events-all``
  file) per line; globbing patterns are accepted too.  This option is only
  available if QEMU has been compiled with the ``simple``, ``log`` or
  ``ftrace`` tracing backend.

``file=FILE``

  Log output traces to *FILE*.
  This option is only available if QEMU has been compiled with
  the ``simple`` tracing backend.
