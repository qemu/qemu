#include "gloffscreen.h"
#include "glextensions.h"

void (*glFrameTerminatorGREMEDY)(void);

void glextensions_init(void)
{
    glFrameTerminatorGREMEDY = glo_get_extension_proc("glFrameTerminatorGREMEDY");
}
