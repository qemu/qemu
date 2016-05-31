// Use macro DIV_ROUND_UP instead of (((n) + (d) - 1) /(d))
@@
expression e1;
expression e2;
@@
(
- ((e1) + e2 - 1) / (e2)
+ DIV_ROUND_UP(e1,e2)
|
- ((e1) + (e2 - 1)) / (e2)
+ DIV_ROUND_UP(e1,e2)
)

@@
expression e1;
expression e2;
@@
-(DIV_ROUND_UP(e1,e2))
+DIV_ROUND_UP(e1,e2)
