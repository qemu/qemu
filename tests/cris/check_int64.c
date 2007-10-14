#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sys.h"
#include "crisutils.h"


extern inline int64_t add64(const int64_t a, const int64_t b) {
	return a + b;
}

extern inline int64_t sub64(const int64_t a, const int64_t b) {
	return a - b;
}

int main(void)
{
	int64_t a = 1;
	int64_t b = 2;

	/* FIXME: add some tests.  */
	a = add64(a, b);
	if (a != 3)
		err();

	a = sub64(a, b);
	if (a != 1)
		err();

	a = add64(a, -4);
	if (a != -3)
		err();

	a = add64(a, 3);
	if (a != 0)
		err();

	a = 0;
	a = sub64(a, 1);
	if (a != -1)
		err();

	pass();
	return 0;
}
