// replace muldiv64(a, 1, b) by "a / b"
@@
expression a, b;
@@
-muldiv64(a, 1, b)
+a / b
