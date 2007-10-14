#include <unistd.h>

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define CURRENT_LOCATION __FILE__ ":" TOSTRING(__LINE__)

#define err()                         \
{                                     \
  _fail("at " CURRENT_LOCATION " ");  \
}

#define mb() asm volatile ("" : : : "memory")

extern void pass(void);
extern void _fail(char *reason);
