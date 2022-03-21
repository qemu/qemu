// Use g_new() & friends where that makes obvious sense
@@
type T;
@@
-g_malloc(sizeof(T))
+g_new(T, 1)
@@
type T;
@@
-g_try_malloc(sizeof(T))
+g_try_new(T, 1)
@@
type T;
@@
-g_malloc0(sizeof(T))
+g_new0(T, 1)
@@
type T;
@@
-g_try_malloc0(sizeof(T))
+g_try_new0(T, 1)
@@
type T;
expression n;
@@
-g_malloc(sizeof(T) * (n))
+g_new(T, n)
@@
type T;
expression n;
@@
-g_try_malloc(sizeof(T) * (n))
+g_try_new(T, n)
@@
type T;
expression n;
@@
-g_malloc0(sizeof(T) * (n))
+g_new0(T, n)
@@
type T;
expression n;
@@
-g_try_malloc0(sizeof(T) * (n))
+g_try_new0(T, n)
@@
type T;
expression p, n;
@@
-g_realloc(p, sizeof(T) * (n))
+g_renew(T, p, n)
@@
type T;
expression p, n;
@@
-g_try_realloc(p, sizeof(T) * (n))
+g_try_renew(T, p, n)
@@
type T;
expression n;
@@
-(T *)g_new(T, n)
+g_new(T, n)
@@
type T;
expression n;
@@
-(T *)g_new0(T, n)
+g_new0(T, n)
@@
type T;
expression p, n;
@@
-(T *)g_renew(T, p, n)
+g_renew(T, p, n)
