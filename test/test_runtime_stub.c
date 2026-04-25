#include "ient_runtime.h"

ENT_CTX* iENT_RuntimeActiveCtx(void)
{
    return &gEntCtx;
}
