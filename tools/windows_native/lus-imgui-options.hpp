#pragma once
#include <SDL.h>
#include "cheat_visibility.hpp"
#include <atomic>
#include <deque>
#include <mutex>
#include "lus-controller-model.hpp"
#include "controller-accessories.hpp"
namespace rush::options {
inline rush::cheats::Visibility* cheat_preferences=nullptr;
inline SDL_Window* window=nullptr;
inline std::atomic<bool> available{false}, visible{false}, controllers{false}, is_fullscreen{false};
inline std::atomic<int> resolution_request{-1}, fullscreen_request{-1}, window_size_request{-1}, bar_height{25};
inline std::atomic<int> page_request{-1}, refresh_request{-1}, vsync_request{-1};
inline std::atomic<uint64_t> window_extent_request{0}, actual_window_extent{0};
inline uint64_t pack_extent(int width,int height){return (uint64_t(uint32_t(width))<<32)|uint32_t(height);}
inline std::atomic<bool> capture_active{false}, actual_vsync{true};
inline std::mutex mutex;
inline std::deque<SDL_Event> events;
inline bool route(const SDL_Event& event) {
    if (!available.load()) return false;
    if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode==SDL_SCANCODE_F1) {
        page_request.store(0);visible.store(true); return true;
    }
    if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode==SDL_SCANCODE_F8){page_request.store(2);visible.store(true);return true;}
    if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode==SDL_SCANCODE_F5){page_request.store(4);visible.store(true);return true;}
    if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode==SDL_SCANCODE_F11){fullscreen_request.store(is_fullscreen.load()?0:1);return true;}
    if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode==SDL_SCANCODE_F2){page_request.store(1);visible.store(true);return true;}
    if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode==SDL_SCANCODE_F9){page_request.store(3);visible.store(true);return true;}
    if(visible.load() && (event.type==SDL_JOYBUTTONDOWN || event.type==SDL_JOYAXISMOTION || event.type==SDL_JOYHATMOTION || event.type==SDL_CONTROLLERBUTTONDOWN || event.type==SDL_CONTROLLERAXISMOTION)){std::lock_guard<std::mutex> lock(mutex);if(events.size()<256)events.push_back(event);return false;}
    if(!visible.load()){
        int y=-1;
        if(event.type==SDL_MOUSEMOTION)y=event.motion.y;
        else if(event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP)y=event.button.y;
        if(y<0 || y>=bar_height.load())return false;
    }
    if(!capture_active.load() && event.type==SDL_KEYDOWN && event.key.keysym.scancode==SDL_SCANCODE_ESCAPE){visible.store(false);return true;}
    bool capture=event.type==SDL_KEYDOWN || event.type==SDL_KEYUP || event.type==SDL_TEXTINPUT ||
        event.type==SDL_MOUSEMOTION || event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP || event.type==SDL_MOUSEWHEEL;
    if(capture){std::lock_guard<std::mutex> lock(mutex);if(events.size()<256)events.push_back(event);}
    return capture;
}
inline std::deque<SDL_Event> drain(){std::lock_guard<std::mutex> lock(mutex);std::deque<SDL_Event> result;result.swap(events);return result;}
}
