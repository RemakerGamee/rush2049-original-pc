#pragma once

#include <SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace rush::keyboard {

struct Sample {
    uint16_t buttons = 0;
    int8_t x = 0;
    int8_t y = 0;
};

class Input {
public:
    void handle_event(const SDL_Event& event) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            pressed_.fill(false);
            delivered_.fill(false);
            queue_head_ = 0;
            queue_size_ = 0;
            release_sample_ = false;
            return;
        }

        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
            const int index = static_cast<int>(event.key.keysym.scancode);
            if (!valid_scancode(index) || pressed_[size_t(index)]) {
                return;
            }
            const Sample value = key_value(event.key.keysym.scancode);
            if (!is_mapped(value)) {
                return;
            }
            pressed_[size_t(index)] = true;
            delivered_[size_t(index)] = false;
            return;
        }

        if (event.type == SDL_KEYUP) {
            const int index = static_cast<int>(event.key.keysym.scancode);
            if (!valid_scancode(index) || !pressed_[size_t(index)]) {
                return;
            }
            if (!delivered_[size_t(index)]) {
                enqueue(key_value(event.key.keysym.scancode));
            }
            pressed_[size_t(index)] = false;
            delivered_[size_t(index)] = false;
        }
    }

    Sample sample() {
        std::lock_guard<std::mutex> guard(mutex_);
        Sample result = held_sample();
        if (release_sample_) {
            release_sample_ = false;
            return result;
        }
        if (queue_size_ == 0) {
            return result;
        }

        const Sample tap = queue_[queue_head_];
        queue_head_ = (queue_head_ + 1) % queue_.size();
        --queue_size_;
        result.buttons |= tap.buttons;
        result.x = clamp_axis(int(result.x) + int(tap.x));
        result.y = clamp_axis(int(result.y) + int(tap.y));
        release_sample_ = true;
        return result;
    }

    uint64_t take_dropped_tap_count() {
        std::lock_guard<std::mutex> guard(mutex_);
        const uint64_t count = dropped_taps_;
        dropped_taps_ = 0;
        return count;
    }

private:
    static constexpr size_t tap_capacity = 64;

    static bool valid_scancode(int index) {
        return index > SDL_SCANCODE_UNKNOWN && index < SDL_NUM_SCANCODES;
    }

    static Sample key_value(SDL_Scancode scancode) {
        switch (scancode) {
        case SDL_SCANCODE_RETURN: return {0x1000, 0, 0};
        case SDL_SCANCODE_Z: return {0x8000, 0, 0};
        case SDL_SCANCODE_X: return {0x4000, 0, 0};
        case SDL_SCANCODE_SPACE: return {0x2000, 0, 0};
        case SDL_SCANCODE_Q: return {0x0020, 0, 0};
        case SDL_SCANCODE_E: return {0x0010, 0, 0};
        case SDL_SCANCODE_I: return {0x0008, 0, 0};
        case SDL_SCANCODE_K: return {0x0004, 0, 0};
        case SDL_SCANCODE_J: return {0x0002, 0, 0};
        case SDL_SCANCODE_L: return {0x0001, 0, 0};
        case SDL_SCANCODE_LEFT: return {0, -1, 0};
        case SDL_SCANCODE_RIGHT: return {0, 1, 0};
        case SDL_SCANCODE_DOWN: return {0, 0, -1};
        case SDL_SCANCODE_UP: return {0, 0, 1};
        default: return {};
        }
    }

    static bool is_mapped(const Sample& value) {
        return value.buttons != 0 || value.x != 0 || value.y != 0;
    }

    static int8_t clamp_axis(int value) {
        return static_cast<int8_t>(value < -1 ? -1 : value > 1 ? 1 : value);
    }

    void enqueue(const Sample& value) {
        if (queue_size_ == queue_.size()) {
            ++dropped_taps_;
            return;
        }
        const size_t tail = (queue_head_ + queue_size_) % queue_.size();
        queue_[tail] = value;
        ++queue_size_;
    }

    Sample held_sample() {
        Sample result{};
        for (int index = 1; index < SDL_NUM_SCANCODES; ++index) {
            if (!pressed_[size_t(index)]) {
                continue;
            }
            delivered_[size_t(index)] = true;
            const Sample value = key_value(static_cast<SDL_Scancode>(index));
            result.buttons |= value.buttons;
            result.x = clamp_axis(int(result.x) + int(value.x));
            result.y = clamp_axis(int(result.y) + int(value.y));
        }
        return result;
    }

    std::mutex mutex_;
    std::array<bool, SDL_NUM_SCANCODES> pressed_{};
    std::array<bool, SDL_NUM_SCANCODES> delivered_{};
    std::array<Sample, tap_capacity> queue_{};
    size_t queue_head_ = 0;
    size_t queue_size_ = 0;
    bool release_sample_ = false;
    uint64_t dropped_taps_ = 0;
};

} // namespace rush::keyboard
