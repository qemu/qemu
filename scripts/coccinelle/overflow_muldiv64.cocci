// Find muldiv64(i64, i64, x) for potential overflow
@filter@
typedef uint64_t;
typedef int64_t;
{ uint64_t, int64_t, long, unsigned long } a, b;
expression c;
position p;
@@

muldiv64(a,b,c)@p

@script:python@
p << filter.p;
@@

cocci.print_main("potential muldiv64() overflow", p)
