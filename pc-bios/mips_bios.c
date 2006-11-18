typedef void (*bios_func)(void);

void __start(void)
{
	bios_func f = (bios_func)0xb0000000;
	f();
}
