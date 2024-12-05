#ifndef __PANDA_CHEADERS_H__
#define __PANDA_CHEADERS_H__

// ugh these are here so that g++ can actually handle gnarly qemu code

#ifdef __cplusplus
#include <type_traits>
#pragma push_macro("new")
#pragma push_macro("typename")
#pragma push_macro("typeof")
#pragma push_macro("export")
#define new pandanew
#define typename
#define typeof(x) std::remove_const<std::remove_reference<decltype(x)>::type>::type
#define export pandaexport

extern "C" {
#endif

#ifdef CONFIG_SOFTMMU
#include "config-host.h"
// #include "config-target.h"
#include "cpu.h"
#endif


#include "panda/common.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"
#include "disas/disas.h"

// Don't forget to undefine it so people can actually use C++ stuff...
#ifdef __cplusplus
}
#pragma pop_macro("new")
#pragma pop_macro("typename")
#pragma pop_macro("typeof")
#pragma pop_macro("export")
#endif

#endif
