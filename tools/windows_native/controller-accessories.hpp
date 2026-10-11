#pragma once
#include <array>
#include <atomic>
namespace rush::accessories {
// Persisted preference: 0=Memory Pak, 1=Rumble Pak. Only changes at startup;
// never detach an open save image during a guest write.
inline std::array<std::atomic<int>,4> mode{};
inline std::array<std::atomic<bool>,4> rumble_supported{},rumble_requested{};
inline std::atomic<bool> memory_attached{false};
inline bool has_rumble(int port){return port>=0 && port<4 && mode[port].load()==1 && rumble_supported[port].load();}
inline void request_rumble(int port,bool active){if(port>=0 && port<4)rumble_requested[port].store(active && has_rumble(port));}
}
