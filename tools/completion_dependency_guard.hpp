#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace rush {
inline bool completion_dependency_poll_site(const char* symbol) {
    // The reproduced cycle is the verified graphics-counter loop at guest 0x80096184.
    // A matching queue snapshot at an unrelated poll is not permission to preempt it.
    return symbol && std::strcmp(symbol, "func_80096130") == 0;
}
inline bool completion_dependency_guest_object(uint32_t address, std::size_t size) {
    constexpr uint32_t ram_size = 0x800000;
    return (address & 0xc0000000u) == 0x80000000u && (address & 3u) == 0 &&
        size <= ram_size && (address & 0x1fffffffu) <= ram_size - size;
}
struct CompletionDependency {
    int pending, self_id, self_priority, count, capacity;
    int sender_id, sender_priority; unsigned sender_queue;
    int ready_id, ready_priority;
};
inline bool completion_dependency_wait(const CompletionDependency& s) {
    return s.pending>0 && s.self_id==7 && s.self_priority==5 && s.count==60 && s.capacity==60 &&
        s.sender_id==4 && s.sender_priority==12 && s.sender_queue==0x8002ecfcu &&
        s.ready_id==6 && s.ready_priority==4;
}
}
