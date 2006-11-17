/* TODO: stop #including, move into wireless.c
 * until then, keep in sync copies in prism54/ and acx/ dirs
 * code+data size: less than 1k */

enum {
	DOT11_RATE_1,
	DOT11_RATE_2,
	DOT11_RATE_5,
	DOT11_RATE_11,
	DOT11_RATE_22,
	DOT11_RATE_33,
	DOT11_RATE_6,
	DOT11_RATE_9,
	DOT11_RATE_12,
	DOT11_RATE_18,
	DOT11_RATE_24,
	DOT11_RATE_36,
	DOT11_RATE_48,
	DOT11_RATE_54
};
enum {
	DOT11_MOD_DBPSK,
	DOT11_MOD_DQPSK,
	DOT11_MOD_CCK,
	DOT11_MOD_OFDM,
	DOT11_MOD_CCKOFDM,
	DOT11_MOD_PBCC
};
static const u8 ratelist[] = { 1,2,5,11,22,33,6,9,12,18,24,36,48,54 };
static const u8 dot11ratebyte[] = { 1*2,2*2,11,11*2,22*2,33*2,6*2,9*2,12*2,18*2,24*2,36*2,48*2,54*2 };
static const u8 default_modulation[] = {
	DOT11_MOD_DBPSK,
	DOT11_MOD_DQPSK,
	DOT11_MOD_CCK,
	DOT11_MOD_CCK,
	DOT11_MOD_PBCC,
	DOT11_MOD_PBCC,
	DOT11_MOD_OFDM,
	DOT11_MOD_OFDM,
	DOT11_MOD_OFDM,
	DOT11_MOD_OFDM,
	DOT11_MOD_OFDM,
	DOT11_MOD_OFDM,
	DOT11_MOD_OFDM,
	DOT11_MOD_OFDM
};

static /* TODO: remove 'static' when moved to wireless.c */
int
rate_mbit2enum(int n) {
	int i=0;
	while(i<sizeof(ratelist)) {
		if(n==ratelist[i]) return i;
		i++;
	}
	return -EINVAL;
}

static int
get_modulation(int r_enum, char suffix) {
	if(suffix==',' || suffix==' ' || suffix=='\0') {
		/* could shorten default_mod by 8 bytes:
		if(r_enum>=DOT11_RATE_6) return DOT11_MOD_OFDM; */
		return default_modulation[r_enum];
	}
	if(suffix=='c') {
		if(r_enum<DOT11_RATE_5 || r_enum>DOT11_RATE_11) return -EINVAL;
		return DOT11_MOD_CCK;
	}
	if(suffix=='p') {
		if(r_enum<DOT11_RATE_5 || r_enum>DOT11_RATE_33) return -EINVAL;
		return DOT11_MOD_PBCC;
	}
	if(suffix=='o') {
		if(r_enum<DOT11_RATE_6) return -EINVAL;
		return DOT11_MOD_OFDM;
	}
	if(suffix=='d') {
		if(r_enum<DOT11_RATE_6) return -EINVAL;
		return DOT11_MOD_CCKOFDM;
	}
	return -EINVAL;
}

#ifdef UNUSED
static int
fill_ratevector(const char **pstr, u8 *vector, int size,
		int (*supported)(int mbit, int mod, void *opaque), void *opaque, int or_mask)
{
	unsigned long rate_mbit;
	int rate_enum,mod;
	const char *str = *pstr;
	char c;

	do {
		rate_mbit = simple_strtoul(str, (char**)&str, 10);
		if(rate_mbit>INT_MAX) return -EINVAL;

		rate_enum = rate_mbit2enum(rate_mbit);
		if(rate_enum<0) return rate_enum;

		c = *str;
		mod = get_modulation(rate_enum, c);
		if(mod<0) return mod;

		if(c>='a' && c<='z') c = *++str;
		if(c!=',' && c!=' ' && c!='\0') return -EINVAL;

		if(supported) {
			int r = supported(rate_mbit, mod, opaque);
			if(r) return r;
		}

		*vector++ = dot11ratebyte[rate_enum] | or_mask;

		size--;
		str++;
	} while(size>0 && c==',');

	if(size<1) return -E2BIG;
	*vector=0; /* TODO: sort, remove dups? */

	*pstr = str-1;
	return 0;
}

static /* TODO: remove 'static' when moved to wireless.c */
int
fill_ratevectors(const char *str, u8 *brate, u8 *orate, int size,
		int (*supported)(int mbit, int mod, void *opaque), void *opaque)
{
	int r;

	r = fill_ratevector(&str, brate, size, supported, opaque, 0x80);
	if(r) return r;

	orate[0] = 0;
	if(*str==' ') {
		str++;
		r = fill_ratevector(&str, orate, size, supported, opaque, 0);
		if(r) return r;
		/* TODO: sanitize, e.g. remove/error on rates already in basic rate set? */
	}
	if(*str)
		return -EINVAL;

	return 0;
}
#endif

/* TODO: use u64 masks? */

static int
fill_ratemask(const char **pstr, u32* mask,
		int (*supported)(int mbit, int mod,void *opaque),
		u32 (*gen_mask)(int mbit, int mod,void *opaque),
		void *opaque)
{
	unsigned long rate_mbit;
	int rate_enum,mod;
	u32 m = 0;
	const char *str = *pstr;
	char c;

	do {
		rate_mbit = simple_strtoul(str, (char**)&str, 10);
		if(rate_mbit>INT_MAX) return -EINVAL;

		rate_enum = rate_mbit2enum(rate_mbit);
		if(rate_enum<0) return rate_enum;

		c = *str;
		mod = get_modulation(rate_enum, c);
		if(mod<0) return mod;

		if(c>='a' && c<='z') c = *++str;
		if(c!=',' && c!=' ' && c!='\0') return -EINVAL;

		if(supported) {
			int r = supported(rate_mbit, mod, opaque);
			if(r) return r;
		}

		m |= gen_mask(rate_mbit, mod, opaque);
		str++;
	} while(c==',');

	*pstr = str-1;
	*mask |= m;
	return 0;
}

static /* TODO: remove 'static' when moved to wireless.c */
int
fill_ratemasks(const char *str, u32 *bmask, u32 *omask,
		int (*supported)(int mbit, int mod,void *opaque),
		u32 (*gen_mask)(int mbit, int mod,void *opaque),
		void *opaque)
{
	int r;

	r = fill_ratemask(&str, bmask, supported, gen_mask, opaque);
	if(r) return r;

	if(*str==' ') {
		str++;
		r = fill_ratemask(&str, omask, supported, gen_mask, opaque);
		if(r) return r;
	}
	if(*str)
		return -EINVAL;
	return 0;
}
