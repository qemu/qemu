// replace muldiv64(i32, i32, x) by (uint64_t)i32 * i32 / x
@@
typedef uint32_t;
typedef int32_t;
{ uint32_t, int32_t, int, unsigned int } a, b;
typedef uint64_t;
expression c;
@@

-muldiv64(a,b,c)
+(uint64_t) a * b / c
