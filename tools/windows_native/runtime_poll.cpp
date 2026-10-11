#include "recomp.h"
#include "ultramodern/ultramodern.hpp"
#include "completion_dependency_guard.hpp"
#include <Windows.h>
#include <intrin.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>

extern "C" void func_80096130(uint8_t*, recomp_context*);
extern "C" void yield_self_1ms(uint8_t*);
extern "C" bool rush_resume_ready_dependency(uint8_t*,int32_t);

// The x64 unwind table identifies the actual containing native function.
// A missing entry fails closed; no approximate address range or guessed symbol.
static bool verified_wait_caller(void* pc) {
    DWORD64 image=0;
    auto* entry=RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(pc),&image,nullptr);
    const bool match=entry && image+entry->BeginAddress==reinterpret_cast<DWORD64>(&func_80096130);
    static std::atomic<bool> reported{false};
    if(match && !reported.exchange(true)) std::puts("COMPLETION_DEPENDENCY_SITE: Windows unwind entry matches exact func_80096130.");
    return match;
}

extern "C" __declspec(noinline) void rush_poll_runtime(uint8_t* rdram) {
    static const bool enabled=[] {
        const char* flag=std::getenv("RUSH_COMPLETION_DEPENDENCY_YIELD");
        return !flag || std::strcmp(flag,"1")==0;
    }();
    if(enabled && rdram && ultramodern::is_game_thread() && verified_wait_caller(_ReturnAddress())) {
        auto* q=TO_PTR(OSMesgQueue,int32_t(0x8002ecf8u));
        auto self_address=ultramodern::this_thread();
        auto ready=ultramodern::thread_queue_peek(rdram,ultramodern::running_queue);
        if(rush::completion_dependency_guest_object(uint32_t(self_address),sizeof(OSThread)) &&
           rush::completion_dependency_guest_object(uint32_t(ready),sizeof(OSThread)) &&
           rush::completion_dependency_guest_object(uint32_t(q->blocked_on_send),sizeof(OSThread))) {
            auto* self=TO_PTR(OSThread,self_address);
            auto* sender=TO_PTR(OSThread,q->blocked_on_send);
            auto* consumer=TO_PTR(OSThread,ready);
            rush::CompletionDependency state{int(MEM_H(0,int32_t(0x8002eb70u))),self->id,self->priority,
                q->validCount,q->msgCount,sender->id,sender->priority,uint32_t(sender->queue),consumer->id,consumer->priority};
            if(rush::completion_dependency_wait(state) && rush_resume_ready_dependency(rdram,ready))
                std::puts("COMPLETION_DEPENDENCY_HANDOFF: exact Windows wait caller; original messages/counter retained.");
        }
    }
    yield_self_1ms(rdram);
}
