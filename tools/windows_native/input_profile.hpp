#pragma once

#include "n64_input.hpp"
#include "controller-metadata.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Mapping profiles, validation, the text configuration format and the pure
// conversion from raw device state to N64 state. This header has no SDL
// dependency so every function can be tested without hardware.
namespace rush::input {

constexpr int config_version = 1;
constexpr int max_index = 255;

enum class BindKind : uint8_t { Button, Hat, Axis };

enum HatDirection : uint8_t { hat_up = 1, hat_right = 2, hat_down = 4, hat_left = 8 }; // SDL_HAT_*

struct Binding {
    BindKind kind = BindKind::Button;
    int index = 0;
    uint8_t hat_mask = 0;   // exactly one HatDirection bit for Hat bindings
    int axis_sign = 1;      // +1 or -1: which deflection direction activates an Axis binding
    float threshold = 0.5f; // (0, 1]: normalized deflection that counts as pressed
};

struct StickAxis {
    bool set = false;
    int axis = 0;
    bool invert = false;
};

struct Profile {
    std::string id;
    std::string display_name,controller_family;
    bool enabled = true;
    bool game_controller = false; // true: indexes are SDL_GameControllerButton/Axis values
    std::string guid;             // 32 lowercase hex characters for raw joystick profiles
    StickAxis stick_x;
    StickAxis stick_y;
    float deadzone = 0.15f;
    float sensitivity = 1.0f;
    std::array<std::vector<Binding>, button_count> bindings;
};

struct Capabilities {
    int buttons = 0;
    int axes = 0;
    int hats = 0;
};

struct RawState {
    std::vector<uint8_t> buttons;
    std::vector<int16_t> axes;
    std::vector<uint8_t> hats; // SDL hat bitmask per hat

    void resize(const Capabilities& caps) {
        buttons.assign(size_t(std::max(0, caps.buttons)), 0);
        axes.assign(size_t(std::max(0, caps.axes)), 0);
        hats.assign(size_t(std::max(0, caps.hats)), 0);
    }
    void neutral() {
        std::fill(buttons.begin(), buttons.end(), uint8_t(0));
        std::fill(axes.begin(), axes.end(), int16_t(0));
        std::fill(hats.begin(), hats.end(), uint8_t(0));
    }
};

// Returns an error description, or std::nullopt when the profile can be applied
// to a device with these capabilities.
inline std::optional<std::string> validate_profile(const Profile& profile, const Capabilities& caps) {
    if (!std::isfinite(profile.sensitivity) || profile.sensitivity<0.1f || profile.sensitivity>2.0f) return "sensitivity out of range";
    if (!valid_deadzone(profile.deadzone)) {
        return "deadzone out of range";
    }
    for (const StickAxis* stick : {&profile.stick_x, &profile.stick_y}) {
        if (stick->set && (stick->axis < 0 || stick->axis >= caps.axes)) {
            return "stick axis " + std::to_string(stick->axis) + " outside device range";
        }
    }
    if (profile.stick_x.set && profile.stick_y.set && profile.stick_x.axis == profile.stick_y.axis) {
        return "stick x and y use the same axis";
    }
    for (size_t button = 0; button < button_count; ++button) {
        for (const Binding& binding : profile.bindings[button]) {
            const std::string name = button_name(Button(button));
            switch (binding.kind) {
            case BindKind::Button:
                if (binding.index < 0 || binding.index >= caps.buttons) {
                    return name + ": button " + std::to_string(binding.index) + " outside device range";
                }
                break;
            case BindKind::Hat:
                if (binding.index < 0 || binding.index >= caps.hats) {
                    return name + ": hat " + std::to_string(binding.index) + " outside device range";
                }
                if (binding.hat_mask != hat_up && binding.hat_mask != hat_right &&
                    binding.hat_mask != hat_down && binding.hat_mask != hat_left) {
                    return name + ": invalid hat direction";
                }
                break;
            case BindKind::Axis:
                if (binding.index < 0 || binding.index >= caps.axes) {
                    return name + ": axis " + std::to_string(binding.index) + " outside device range";
                }
                if (binding.axis_sign != 1 && binding.axis_sign != -1) {
                    return name + ": invalid axis direction";
                }
                if (!(binding.threshold > 0.0f && binding.threshold <= 1.0f)) {
                    return name + ": threshold out of range";
                }
                break;
            }
        }
    }
    return std::nullopt;
}

inline bool binding_active(const Binding& binding, const RawState& raw) {
    switch (binding.kind) {
    case BindKind::Button:
        return binding.index >= 0 && size_t(binding.index) < raw.buttons.size() && raw.buttons[size_t(binding.index)] != 0;
    case BindKind::Hat:
        return binding.index >= 0 && size_t(binding.index) < raw.hats.size() &&
               (raw.hats[size_t(binding.index)] & binding.hat_mask) != 0;
    case BindKind::Axis:
        if (binding.index < 0 || size_t(binding.index) >= raw.axes.size()) {
            return false;
        }
        return normalize_axis(raw.axes[size_t(binding.index)]) * float(binding.axis_sign) >= binding.threshold;
    }
    return false;
}

inline float stick_value(const StickAxis& stick, const RawState& raw, float deadzone) {
    if (!stick.set || stick.axis < 0 || size_t(stick.axis) >= raw.axes.size()) {
        return 0.0f;
    }
    float value = normalize_axis(raw.axes[size_t(stick.axis)]);
    if (stick.invert) {
        value = -value;
    }
    return apply_deadzone(value, deadzone);
}

// Pure conversion. The caller owns the `connected` flag.
inline PortState convert_state(const Profile& profile, const RawState& raw) {
    PortState state;
    if(!profile.enabled)return state;
    for (size_t button = 0; button < button_count; ++button) {
        for (const Binding& binding : profile.bindings[button]) {
            if (binding_active(binding, raw)) {
                state.buttons |= button_bit(Button(button));
                break;
            }
        }
    }
    state.x = std::clamp(stick_value(profile.stick_x, raw, profile.deadzone)*profile.sensitivity,-1.0f,1.0f);
    state.y = std::clamp(stick_value(profile.stick_y, raw, profile.deadzone)*profile.sensitivity,-1.0f,1.0f);
    return state;
}

struct Config {
    int version = config_version;
    int keyboard_port = 0; // -1: keyboard not assigned to any port
    float deadzone = 0.15f;
    std::map<std::string, int> assignments; // device GUID -> port
    std::vector<Profile> profiles;
};

struct ConfigResult {
    Config config;
    std::vector<std::string> errors; // non-empty: the whole file is rejected
    bool ok() const { return errors.empty(); }
};

inline bool is_guid(std::string_view text) {
    return text.size() == 32 && std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

namespace detail {

inline bool parse_int(std::string_view text, int minimum, int maximum, int& out) {
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size() || value < minimum || value > maximum) {
        return false;
    }
    out = value;
    return true;
}

inline bool parse_float(std::string_view text, float& out) {
    float value = 0.0f;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size() || !std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

inline std::vector<std::string_view> tokens(std::string_view line) {
    std::vector<std::string_view> out;
    size_t position = 0;
    while (position < line.size()) {
        while (position < line.size() && (line[position] == ' ' || line[position] == '\t' || line[position] == '\r')) {
            ++position;
        }
        if (position >= line.size() || line[position] == '#') {
            break;
        }
        const size_t start = position;
        while (position < line.size() && line[position] != ' ' && line[position] != '\t' &&
               line[position] != '\r' && line[position] != '#') {
            ++position;
        }
        out.push_back(line.substr(start, position - start));
    }
    return out;
}

} // namespace detail

// Text format (one directive per line, '#' starts a comment):
//   rush-input 1
//   keyboard-port 0|1|2|3|none
//   deadzone 0.15                      (default for profiles; 0 .. 0.9)
//   assign <32-hex-guid> <port>        (pin a device to a port)
//   profile <id> guid <32-hex-guid>    (raw joystick profile for that device GUID)
//   profile <id> gamecontroller        (overrides the built-in SDL game-controller profile)
//     deadzone <f>
//     stick-x axis <n> [invert]
//     stick-y axis <n> [invert]
//     bind <A|B|Start|Z|L|R|D-Up|D-Down|D-Left|D-Right|C-Up|C-Down|C-Left|C-Right> button <n>
//     bind <button> hat <n> up|right|down|left
//     bind <button> axis <n> +|- [threshold]
// Directives after a `profile` line belong to that profile; global directives
// (keyboard-port, assign) must precede the first profile. Indexes are checked
// against the real device when the profile is applied. Any error rejects the
// entire file so a partial mapping is never applied.
inline ConfigResult parse_config(std::string_view text) {
    ConfigResult result;
    Config& config = result.config;
    bool seen_header = false;
    bool in_profile = false;
    int line_number = 0;
    const auto fail = [&](const std::string& message) {
        result.errors.push_back("line " + std::to_string(line_number) + ": " + message);
    };

    size_t cursor = 0;
    while (cursor <= text.size()) {
        const size_t end = std::min(text.find('\n', cursor), text.size());
        const std::string_view line = text.substr(cursor, end - cursor);
        cursor = end + 1;
        ++line_number;
        const auto words = detail::tokens(line);
        if (words.empty()) {
            continue;
        }
        const std::string_view directive = words[0];
        if (!seen_header) {
            int version = 0;
            if (directive != "rush-input" || words.size() != 2 || !detail::parse_int(words[1], 0, 1000000, version)) {
                fail("first directive must be 'rush-input <version>'");
                return result;
            }
            if (version != config_version) {
                fail("unsupported configuration version " + std::to_string(version));
                return result;
            }
            config.version = version;
            seen_header = true;
            continue;
        }

        if (directive == "keyboard-port") {
            int port = 0;
            if (in_profile) {
                fail("keyboard-port must precede profiles");
            } else if (words.size() == 2 && words[1] == "none") {
                config.keyboard_port = -1;
            } else if (words.size() == 2 && detail::parse_int(words[1], 0, port_count - 1, port)) {
                config.keyboard_port = port;
            } else {
                fail("keyboard-port needs 0-" + std::to_string(port_count - 1) + " or none");
            }
        } else if (directive == "assign") {
            int port = 0;
            if (in_profile) {
                fail("assign must precede profiles");
                continue;
            }
            if (words.size() != 3 || !is_guid(words[1]) || !detail::parse_int(words[2], 0, port_count - 1, port)) {
                fail("assign needs <32-hex-guid> <port 0-" + std::to_string(port_count - 1) + ">");
                continue;
            }
            const std::string guid(words[1]);
            if (config.assignments.count(guid)) {
                fail("duplicate assignment for guid");
                continue;
            }
            bool port_taken = false;
            for (const auto& entry : config.assignments) {
                port_taken = port_taken || entry.second == port;
            }
            if (port_taken) {
                fail("port " + std::to_string(port) + " assigned to two devices");
                continue;
            }
            config.assignments[guid] = port;
        } else if (directive == "deadzone") {
            float deadzone = 0.0f;
            if (words.size() != 2 || !detail::parse_float(words[1], deadzone) || !valid_deadzone(deadzone)) {
                fail("deadzone needs a number from 0 to 0.9");
                continue;
            }
            if (in_profile) {
                config.profiles.back().deadzone = deadzone;
            } else {
                config.deadzone = deadzone;
            }
        } else if(directive=="display-name-hex"){
            std::string value;if(!in_profile || words.size()!=2 || !name_unhex(words[1],value))fail("Invalid controller display name");else config.profiles.back().display_name=value;
        } else if(directive=="controller-type"){
            if(!in_profile || words.size()!=2 || !valid_controller_family(words[1]))fail("Invalid controller type");else config.profiles.back().controller_family=std::string(words[1]);
        } else if (directive == "enabled") {
            int value=0;
            if(!in_profile || words.size()!=2 || !detail::parse_int(words[1],0,1,value))fail("enabled needs a profile and 0 or 1");
            else config.profiles.back().enabled=value!=0;
        } else if (directive == "sensitivity") {
            float value=0;
            if(!in_profile || words.size()!=2 || !detail::parse_float(words[1],value) || value<0.1f || value>2.0f)fail("sensitivity needs a profile and 0.1 to 2.0");
            else config.profiles.back().sensitivity=value;
        } else if (directive == "profile") {
            Profile profile;
            profile.deadzone = config.deadzone;
            in_profile = false;
            if (words.size() < 3 || words.size() > 5) {
                fail("profile needs '<id> guid <hex>' or '<id> gamecontroller'");
                continue;
            }
            profile.id = std::string(words[1]);
            if (words[2] == "gamecontroller" && (words.size()==3 || (words.size()==5 && words[3]=="guid" && is_guid(words[4])))) {
                profile.game_controller = true;
                if(words.size()==5)profile.guid=std::string(words[4]);
            } else if (words[2] == "guid" && words.size() == 4 && is_guid(words[3])) {
                profile.guid = std::string(words[3]);
            } else {
                fail("profile selector must be 'guid <32 lowercase hex>' or 'gamecontroller'");
                continue;
            }
            bool duplicate = false;
            for (const Profile& existing : config.profiles) {
                duplicate = duplicate || existing.id == profile.id ||
                            (profile.game_controller && existing.game_controller && profile.guid==existing.guid) ||
                            (!profile.guid.empty() && existing.guid == profile.guid);
            }
            if (duplicate) {
                fail("duplicate profile id, guid or gamecontroller profile");
                continue;
            }
            config.profiles.push_back(std::move(profile));
            in_profile = true;
        } else if (directive == "stick-x" || directive == "stick-y") {
            if (!in_profile) {
                fail(std::string(directive) + " outside a profile");
                continue;
            }
            int axis = 0;
            if (words.size() < 3 || words.size() > 4 || words[1] != "axis" ||
                !detail::parse_int(words[2], 0, max_index, axis) || (words.size() == 4 && words[3] != "invert")) {
                fail(std::string(directive) + " needs 'axis <n> [invert]'");
                continue;
            }
            StickAxis& stick = directive == "stick-x" ? config.profiles.back().stick_x : config.profiles.back().stick_y;
            stick.set = true;
            stick.axis = axis;
            stick.invert = words.size() == 4;
        } else if (directive == "bind") {
            if (!in_profile) {
                fail("bind outside a profile");
                continue;
            }
            Button button{};
            if (words.size() < 4 || !parse_button(words[1], button)) {
                fail("bind needs a valid N64 button name and source");
                continue;
            }
            Binding binding;
            int index = 0;
            if (words[2] == "button" && words.size() == 4 && detail::parse_int(words[3], 0, max_index, index)) {
                binding.kind = BindKind::Button;
                binding.index = index;
            } else if (words[2] == "hat" && words.size() == 5 && detail::parse_int(words[3], 0, max_index, index)) {
                binding.kind = BindKind::Hat;
                binding.index = index;
                if (words[4] == "up") binding.hat_mask = hat_up;
                else if (words[4] == "right") binding.hat_mask = hat_right;
                else if (words[4] == "down") binding.hat_mask = hat_down;
                else if (words[4] == "left") binding.hat_mask = hat_left;
                else {
                    fail("hat direction must be up, right, down or left");
                    continue;
                }
            } else if (words[2] == "axis" && (words.size() == 5 || words.size() == 6) &&
                       detail::parse_int(words[3], 0, max_index, index) && (words[4] == "+" || words[4] == "-")) {
                binding.kind = BindKind::Axis;
                binding.index = index;
                binding.axis_sign = words[4] == "+" ? 1 : -1;
                if (words.size() == 6 && (!detail::parse_float(words[5], binding.threshold) ||
                                          !(binding.threshold > 0.0f && binding.threshold <= 1.0f))) {
                    fail("axis threshold must be greater than 0 and at most 1");
                    continue;
                }
            } else {
                fail("malformed binding");
                continue;
            }
            config.profiles.back().bindings[size_t(button)].push_back(binding);
        } else {
            fail("unknown directive '" + std::string(directive) + "'");
        }
    }
    if (!seen_header) {
        result.errors.push_back("empty configuration");
    }
    for (const Profile& profile : config.profiles) {
        const bool has_binding = std::any_of(profile.bindings.begin(), profile.bindings.end(),
                                             [](const auto& list) { return !list.empty(); });
        if (profile.enabled && !has_binding && !profile.stick_x.set && !profile.stick_y.set) {
            result.errors.push_back("profile '" + profile.id + "' has no bindings");
        }
    }
    return result;
}

// Built-in profile for devices SDL recognises as a game controller. Index
// values are SDL_GameControllerButton / SDL_GameControllerAxis, independent of
// model-specific raw indices. Shared Xbox layout requested by Scott: A=A, B=X,
// Z=right trigger, shoulders=L/R, Menu=Start, left stick=stick, right stick=C.
// Explicit saved profiles take precedence; raw N64-shaped pads use GUID profiles.
inline Profile default_game_controller_profile() {
    // SDL_CONTROLLER_BUTTON_*: A=0 B=1 X=2 Y=3 BACK=4 GUIDE=5 START=6 LSTICK=7 RSTICK=8
    // LEFTSHOULDER=9 RIGHTSHOULDER=10 DPAD_UP=11 DOWN=12 LEFT=13 RIGHT=14
    // SDL_CONTROLLER_AXIS_*: LEFTX=0 LEFTY=1 RIGHTX=2 RIGHTY=3 TRIGGERLEFT=4 TRIGGERRIGHT=5
    Profile profile;
    profile.id = "builtin-gamecontroller";
    profile.game_controller = true;
    profile.stick_x = {true, 0, false};
    profile.stick_y = {true, 1, true}; // SDL Y is down-positive
    const auto button = [&](Button target, int index) {
        profile.bindings[size_t(target)].push_back({BindKind::Button, index, 0, 1, 0.5f});
    };
    const auto axis = [&](Button target, int index, int sign) {
        profile.bindings[size_t(target)].push_back({BindKind::Axis, index, 0, sign, 0.5f});
    };
    button(Button::A, 0);
    button(Button::B, 2);
    button(Button::Start, 6);
    button(Button::L, 9);
    button(Button::R, 10);
    button(Button::DUp, 11);
    button(Button::DDown, 12);
    button(Button::DLeft, 13);
    button(Button::DRight, 14);
    axis(Button::Z, 5, 1);
    axis(Button::CUp, 3, -1);
    axis(Button::CDown, 3, 1);
    axis(Button::CLeft, 2, -1);
    axis(Button::CRight, 2, 1);
    return profile;
}

} // namespace rush::input
