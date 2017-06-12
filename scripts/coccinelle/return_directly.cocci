// replace 'R = X; return R;' with 'return X;'
@@
identifier VAR;
expression E;
type T;
identifier F;
@@
 T F(...)
 {
     ...
-    T VAR;
     ... when != VAR

-    VAR =
+    return
     E;
-    return VAR;
     ... when != VAR
 }
