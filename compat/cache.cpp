#include "recomp.h"

extern "C" void osInvalICache_recomp(uint8_t*, recomp_context*);

// Forward to the runtime's existing host cache implementation. This runtime
// stores guest RDRAM coherently and does not emulate an N64 instruction cache.
extern "C" void osInvalICache_full(uint8_t* rdram, recomp_context* ctx) {
    osInvalICache_recomp(rdram, ctx);
}
