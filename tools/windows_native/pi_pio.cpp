#include "recomp.h"
#include <span>
#include <cstdint>
namespace recomp { std::span<const uint8_t> get_rom(); }

// Native 800081D0 is public osPiReadIo: acquire PI access, raw ROM read,
// release PI access. Host ROM storage is coherent and needs no N64 MMIO.
extern "C" void rush_real_osPiReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t offset = uint32_t(ctx->r4);
    const uint32_t destination = uint32_t(ctx->r5);
    const uint32_t physical = destination & 0x1FFFFFFFu;
    const auto rom = recomp::get_rom();
    if ((offset & 3) || (physical & 3) || (destination & 0xC0000000u) != 0x80000000u ||
        physical > 0x800000u - 4 || offset > rom.size() || rom.size() - offset < 4) {
        ctx->r2 = uint64_t(-1);
        return;
    }
    const int64_t cached = int64_t(int32_t(physical | 0x80000000u));
    for (unsigned i = 0; i < 4; ++i) MEM_B(i, cached) = rom[offset + i];
    ctx->r2 = 0;
}
