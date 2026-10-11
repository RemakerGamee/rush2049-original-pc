#include "librecomp/rsp.hpp"

extern RspExitReason rush_audio_rsp(uint8_t*, uint32_t);

// Graphics tasks use RT64; only Rush's verified type-2 mixer uses this path.
static RspUcodeFunc* select_rush_microcode(const OSTask* task) {
    if (!task || task->t.type != 2) return nullptr;
    if ((task->t.ucode & 0x1FFFFFFF) != 0x2A050 ||
        (task->t.ucode_data & 0x1FFFFFFF) != 0x2E210 ||
        task->t.ucode_size != 0x1000 || task->t.ucode_data_size != 0x800)
        return nullptr;
    return rush_audio_rsp;
}

recomp::rsp::callbacks_t rush_rsp_callbacks() {
    return {select_rush_microcode};
}

void rush_install_rsp_callbacks() {
    recomp::rsp::set_callbacks(rush_rsp_callbacks());
}
