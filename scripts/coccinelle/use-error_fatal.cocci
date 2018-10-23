@@
type T;
identifier FUN, RET;
expression list ARGS;
expression ERR, EC, FAIL;
@@
(
-    T RET = FUN(ARGS, &ERR);
+    T RET = FUN(ARGS, &error_fatal);
|
-    RET = FUN(ARGS, &ERR);
+    RET = FUN(ARGS, &error_fatal);
|
-    FUN(ARGS, &ERR);
+    FUN(ARGS, &error_fatal);
)
-    if (FAIL) {
-        error_report_err(ERR);
-        exit(EC);
-    }
