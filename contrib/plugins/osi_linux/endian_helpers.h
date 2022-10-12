// OSI Linux works with a bunch of pointers which we need to
// flip if the guest/host endianness mismatch.
//

#if defined(TARGET_WORDS_BIGENDIAN) != defined(HOST_WORDS_BIGENDIAN)
// If guest and host endianness don't match:
// fixupendian will flip a dword in place
#define fixupendian(x)         {x=bswap32((target_ptr_t)x);}
#define fixupendian64(x)       {x=bswap64((uint64_t)x);}
// of flipbadendian will flip a dword
#define flipbadendian(x)       bswap32((target_ptr_t)x)
#define flipbadendian64(x)     bswap64((uint64_t)x)


#else
#define fixupendian(x)         {}
#define fixupendian64(x)       {}
#define flipbadendian(x)       x
#define flipbadendian64(x)     x
#endif

