// replace muldiv64(i32, i64, x) by muldiv64(i64, i32, x)
@@
typedef uint64_t;
typedef int64_t;
typedef uint32_t;
typedef int32_t;
{ uint32_t, int32_t, int, unsigned int } a;
{ uint64_t, int64_t, long, unsigned long } b;
expression c;
@@

-muldiv64(a,b,c)
+muldiv64(b,a,c)
