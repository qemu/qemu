// Remove superfluous timer_del() calls
//
// Copyright Linaro Limited 2020
// This work is licensed under the terms of the GNU GPLv2 or later.
//
// spatch --macro-file scripts/cocci-macro-file.h \
//        --sp-file scripts/coccinelle/timer-del-timer-free.cocci \
//        --in-place --dir .
//
// The timer_free() function now implicitly calls timer_del()
// for you, so calls to timer_del() immediately before the
// timer_free() of the same timer can be deleted.

@@
expression T;
@@
-timer_del(T);
 timer_free(T);
