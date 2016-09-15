// Remove useless casts
@@
type T;
T v;
@@
-	(T *)&v
+	&v
