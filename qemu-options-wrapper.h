
#if defined(QEMU_OPTIONS_GENERATE_ENUM)

#define DEF(option, opt_arg, opt_enum, opt_help, arch_mask)     \
    opt_enum,
#define DEFHEADING(text)

#elif defined(QEMU_OPTIONS_GENERATE_HELP)

#define DEF(option, opt_arg, opt_enum, opt_help, arch_mask)     \
        opt_help
#define DEFHEADING(text) stringify(text) "\n"

#elif defined(QEMU_OPTIONS_GENERATE_OPTIONS)

#define DEF(option, opt_arg, opt_enum, opt_help, arch_mask)     \
    { option, opt_arg, opt_enum, arch_mask },
#define DEFHEADING(text)

#else
#error "qemu-options-wrapper.h included with no option defined"
#endif

#include "qemu-options.def"

#undef DEF
#undef DEFHEADING
#undef GEN_DOCS

#undef QEMU_OPTIONS_GENERATE_ENUM
#undef QEMU_OPTIONS_GENERATE_HELP
#undef QEMU_OPTIONS_GENERATE_OPTIONS
