// Error messages should not contain newlines.  This script finds
// messages that do.  Fixing them is manual.
@r@
expression errp, eno, cls, fmt;
position p;
@@
(
error_report(fmt, ...)@p
|
error_setg(errp, fmt, ...)@p
|
error_setg_errno(errp, eno, fmt, ...)@p
|
error_setg_win32(errp, eno, cls, fmt, ...)@p
|
error_prepend(errp, fmt, ...)@p
|
error_setg_file_open(errp, eno, cls, fmt, ...)@p
|
error_reportf_err(errp, fmt, ...)@p
|
error_set(errp, cls, fmt, ...)@p
)
@script:python@
fmt << r.fmt;
p << r.p;
@@
if "\\n" in str(fmt):
    print "%s:%s:%s:%s" % (p[0].file, p[0].line, p[0].column, fmt)
