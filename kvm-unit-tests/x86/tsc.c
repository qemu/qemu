#include "libcflat.h"
#include "processor.h"

void test_wrtsc(u64 t1)
{
	u64 t2;

	wrtsc(t1);
	t2 = rdtsc();
	printf("rdtsc after wrtsc(%lld): %lld\n", t1, t2);
}

int main()
{
	u64 t1, t2;

	t1 = rdtsc();
	t2 = rdtsc();
	printf("rdtsc latency %lld\n", (unsigned)(t2 - t1));

	test_wrtsc(0);
	test_wrtsc(100000000000ull);
	return 0;
}
