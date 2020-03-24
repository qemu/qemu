// Find and fix trivial use-after-free of Error objects
//
// Copyright (c) 2020 Virtuozzo International GmbH.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see
// <http://www.gnu.org/licenses/>.
//
// How to use:
// spatch --sp-file scripts/coccinelle/error-use-after-free.cocci \
//  --macro-file scripts/cocci-macro-file.h --in-place \
//  --no-show-diff ( FILES... | --use-gitgrep . )

@ exists@
identifier fn, fn2;
expression err;
@@

 fn(...)
 {
     <...
(
     error_free(err);
+    err = NULL;
|
     error_report_err(err);
+    err = NULL;
|
     error_reportf_err(err, ...);
+    err = NULL;
|
     warn_report_err(err);
+    err = NULL;
|
     warn_reportf_err(err, ...);
+    err = NULL;
)
     ... when != err = NULL
         when != exit(...)
     fn2(..., err, ...)
     ...>
 }
