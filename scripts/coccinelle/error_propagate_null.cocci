// error_propagate() already ignores local_err==NULL, so there's
// no need to check it before calling.

@@
identifier L;
expression E;
@@
-if (L) {
     error_propagate(E, L);
-}
