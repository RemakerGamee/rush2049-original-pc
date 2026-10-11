#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

// N64 controller state vocabulary and device-independent conversion helpers.
// Bit values match the runtime's OSContPad.button definitions
// (N64ModernRuntime ultramodern osContGetReadData / libultra PR/os_cont.h).
namespace rush::input {

constexpr int port_count = 4; // MAXCONTROLLERS in the runtime

namespace n64 {
constexpr uint16_t c_right = 0x0001;
constexpr uint16_t c_left = 0x0002;
constexpr uint16_t c_down = 0x0004;
constexpr uint16_t c_up = 0x0008;
constexpr uint16_t r = 0x0010;
constexpr uint16_t l = 0x0020;
constexpr uint16_t d_right = 0x0100;
constexpr uint16_t d_left = 0x0200;
constexpr uint16_t d_down = 0x0400;
constexpr uint16_t d_up = 0x0800;
constexpr uint16_t start = 0x1000;
constexpr uint16_t z = 0x2000;
constexpr uint16_t b = 0x4000;
constexpr uint16_t a = 0x8000;
constexpr uint16_t all = c_right | c_left | c_down | c_up | r | l | d_right | d_left | d_down |
                         d_up | start | z | b | a;
} // namespace n64

enum class Button : uint8_t {
    A, B, Start, Z, L, R, DUp, DDown, DLeft, DRight, CUp, CDown, CLeft, CRight, Count
};
constexpr size_t button_count = size_t(Button::Count);

struct ButtonInfo {
    Button button;
    const char* name;
    uint16_t bit;
};

// Names are the config-file vocabulary. Native Rush semantics: C-Up is Abort and
// C-Left is Reverse; the names deliberately stay N64 names, not modern-pad names.
constexpr std::array<ButtonInfo, button_count> button_table{{
    {Button::A, "A", n64::a},
    {Button::B, "B", n64::b},
    {Button::Start, "Start", n64::start},
    {Button::Z, "Z", n64::z},
    {Button::L, "L", n64::l},
    {Button::R, "R", n64::r},
    {Button::DUp, "D-Up", n64::d_up},
    {Button::DDown, "D-Down", n64::d_down},
    {Button::DLeft, "D-Left", n64::d_left},
    {Button::DRight, "D-Right", n64::d_right},
    {Button::CUp, "C-Up", n64::c_up},
    {Button::CDown, "C-Down", n64::c_down},
    {Button::CLeft, "C-Left", n64::c_left},
    {Button::CRight, "C-Right", n64::c_right},
}};

constexpr uint16_t button_bit(Button button) { return button_table[size_t(button)].bit; }
constexpr const char* button_name(Button button) { return button_table[size_t(button)].name; }

inline bool parse_button(std::string_view text, Button& out) {
    for (const auto& info : button_table) {
        if (text == info.name) {
            out = info.button;
            return true;
        }
    }
    return false;
}

// What the runtime's get_input callback receives: stick values are floats in a
// unit disc (the runtime clamps the magnitude to 1 and applies the N64 octagon),
// +x right and +y up.
struct PortState {
    bool connected = false;
    uint16_t buttons = 0;
    float x = 0.0f;
    float y = 0.0f;
};

constexpr float max_deadzone = 0.90f;

inline bool valid_deadzone(float deadzone) {
    return std::isfinite(deadzone) && deadzone >= 0.0f && deadzone <= max_deadzone;
}

// Symmetric normalization: -32768 and -32767 both give -1.0, +32767 gives +1.0.
inline float normalize_axis(int16_t raw) {
    const int value = raw;
    if (value >= 0) {
        return float(value) / 32767.0f;
    }
    return std::fmax(-1.0f, float(value) / 32767.0f);
}

// Per-axis center deadzone with the live range rescaled to [0, 1] so there is no
// jump at the deadzone edge. |v| <= deadzone gives exactly 0.
inline float apply_deadzone(float value, float deadzone) {
    if (!std::isfinite(value) || !valid_deadzone(deadzone)) {
        return 0.0f;
    }
    const float magnitude = std::fabs(value);
    if (magnitude <= deadzone) {
        return 0.0f;
    }
    const float scaled = std::fmin(1.0f, (magnitude - deadzone) / (1.0f - deadzone));
    return value < 0.0f ? -scaled : scaled;
}

inline float sanitize_axis(float value) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return std::fmax(-1.0f, std::fmin(1.0f, value));
}

// Combination policy for sources sharing one port (keyboard + controller):
// buttons are ORed; the stick comes from whichever source is deflected further.
// A neutral source never erases an active one. Ties keep the first argument.
inline PortState combine(const PortState& first, const PortState& second) {
    PortState result;
    result.connected = first.connected || second.connected;
    result.buttons = uint16_t((first.buttons | second.buttons) & n64::all);
    const float first_magnitude = first.x * first.x + first.y * first.y;
    const float second_magnitude = second.x * second.x + second.y * second.y;
    const PortState& stick = second_magnitude > first_magnitude ? second : first;
    result.x = sanitize_axis(stick.x);
    result.y = sanitize_axis(stick.y);
    return result;
}

} // namespace rush::input
