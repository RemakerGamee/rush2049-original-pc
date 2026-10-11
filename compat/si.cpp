#include "recomp.h"
#include "../N64ModernRuntime/ultramodern/include/ultramodern/ultra64.h"
#include "si_protocol.hpp"
#include <mutex>
#include <cstring>

namespace ultramodern { void send_si_message(); }
namespace {
std::mutex transfer_mutex;
rush_si::Packet request{};
rush_si::State accessories{};

std::array<rush_si::Port, 4> snapshot(bool poll, uint8_t* rdram) {
    // Runtime structs use its own native word-swapped layout. Local scratch
    // RDRAM keeps queries and rumble setup from overwriting game memory.
    alignas(16) uint8_t scratch[1024]{};
    constexpr int32_t scratch_address = int32_t(0x80000000u);
    osContGetQuery(scratch, scratch_address);
    auto* status = reinterpret_cast<OSContStatus*>(scratch);
    OSContPad pads[4]{};
    for (auto& pad : pads) pad.err_no = 8;
    if (poll) {
        // This polls input and delivers this read transfer's SI completion.
        osContStartReadData(rdram, 0);
        osContGetReadData(pads);
    }
    std::array<rush_si::Port, 4> result{};
    for (unsigned i = 0; i < 4; ++i) {
        result[i].connected = status[i].err_no == 0;
        result[i].rumble = result[i].connected && osMotorInit(scratch, 0, scratch_address + 64, i) == 0;
        if (poll && pads[i].err_no == 0) {
            result[i].buttons = pads[i].button;
            result[i].x = pads[i].stick_x;
            result[i].y = pads[i].stick_y;
        } else if (poll) result[i].connected = false;
    }
    return result;
}
void rumble(unsigned channel, bool active) {
    alignas(16) uint8_t scratch[1024]{};
    const auto address = int32_t(0x80000000u);
    if (osMotorInit(scratch, 0, address, channel) == 0)
        __osMotorAccess(scratch, address, active);
}
}

extern "C" void __osSiRawStartDma_recomp(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t direction = uint32_t(ctx->r4);
    const uint32_t address = uint32_t(ctx->r5);
    const uint32_t physical = address & 0x1FFFFFFFu;
    // Support the cached/uncached RDRAM aliases used by this game. Invalid
    // transfers return -1 without touching memory or issuing completion.
    if (direction > 1 || (address & 0xC0000000u) != 0x80000000u ||
        physical > 0x800000u - 64 || (physical & 7)) {
        ctx->r2 = uint64_t(-1); return;
    }
    bool completed_by_poll = false;
    const int64_t cached_address = int64_t(int32_t(physical | 0x80000000u));
    {
        std::lock_guard<std::mutex> lock(transfer_mutex);
        if (direction == 1) {
            for (unsigned i = 0; i < 64; ++i) request[i] = uint8_t(MEM_B(i, cached_address));
        } else {
            completed_by_poll = rush_si::needs_poll(request);
            const auto ports = snapshot(completed_by_poll, rdram);
            const auto response = rush_si::execute(request, ports, accessories, rumble);
            for (unsigned i = 0; i < 64; ++i) MEM_B(i, cached_address) = response[i];
        }
    }
    ctx->r2 = 0;
    if (!completed_by_poll) ultramodern::send_si_message();
}
