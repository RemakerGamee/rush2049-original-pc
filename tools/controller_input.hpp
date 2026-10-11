#pragma once

#include "controller_manager.hpp"
#include "keyboard_input.hpp"
#include "input_remap.hpp"
#include <atomic>

#include <SDL.h>

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

// SDL backend and the keyboard+controller router used by the runtime callbacks.
// Devices are opened once, from SDL_JOYDEVICEADDED, and identified by instance
// id. Game controllers are opened through the game-controller API, everything
// else through the raw joystick API, never both.
namespace rush::input {

class SdlBackend : public Backend {
public:
    // Explicit GUID profiles use physical joystick indices even when SDL has
    // a generic mapping for this N64-shaped pad. Unrelated pads keep defaults.
    explicit SdlBackend(const Config& config = {}) { configure_profiles(config); }
    void configure_profiles(const Config& config) {
        if (!opened_.empty()) throw std::logic_error("Close backend devices before changing profiles.");
        raw_profile_guids_.clear();
        for (const Profile& profile : config.profiles) {
            if (!profile.game_controller && !profile.guid.empty())
                raw_profile_guids_.push_back(profile.guid);
        }
    }
    OpenResult open(int device_index, DeviceInfo& info) override {
        const SDL_JoystickID instance = SDL_JoystickGetDeviceInstanceID(device_index);
        if (instance < 0) {
            return OpenResult::Failed;
        }
        info.instance = instance;
        if (opened_.count(instance)) {
            return OpenResult::AlreadyOpen;
        }
        Opened device;
        char selected_guid[33] = {};
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(device_index), selected_guid, sizeof(selected_guid));
        const bool explicit_raw_profile = std::find(raw_profile_guids_.begin(), raw_profile_guids_.end(),
                                                    selected_guid) != raw_profile_guids_.end();
        const bool game_controller = !explicit_raw_profile && SDL_IsGameController(device_index) == SDL_TRUE;
        SDL_Joystick* joystick = nullptr;
        if (game_controller) {
            device.controller = SDL_GameControllerOpen(device_index);
            joystick = device.controller ? SDL_GameControllerGetJoystick(device.controller) : nullptr;
        } else {
            device.joystick = SDL_JoystickOpen(device_index);
            joystick = device.joystick;
        }
        if (!joystick) {
            return OpenResult::Failed;
        }
        const char* name = game_controller ? SDL_GameControllerName(device.controller) : SDL_JoystickName(joystick);
        char guid[33] = {};
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), guid, sizeof(guid));
        const char* serial = SDL_JoystickGetSerial(joystick);
        info.name = name ? name : "";
        info.guid = guid;
        info.serial = serial ? serial : "";
        info.game_controller = game_controller;
        if (game_controller) {
            info.caps = {SDL_CONTROLLER_BUTTON_MAX, SDL_CONTROLLER_AXIS_MAX, 0};
        } else {
            info.caps = {std::max(0, SDL_JoystickNumButtons(joystick)), std::max(0, SDL_JoystickNumAxes(joystick)),
                         std::max(0, SDL_JoystickNumHats(joystick))};
        }
        opened_[instance] = device;
        return OpenResult::Opened;
    }

    void close(int64_t instance) override {
        const auto it = opened_.find(instance);
        if (it == opened_.end()) {
            return;
        }
        if (it->second.controller) {
            SDL_GameControllerClose(it->second.controller);
        }
        if (it->second.joystick) {
            SDL_JoystickClose(it->second.joystick);
        }
        opened_.erase(it);
    }

    bool read_state(int64_t instance, RawState& state) override {
        const auto it = opened_.find(instance);
        if (it == opened_.end()) {
            return false;
        }
        if (it->second.controller) {
            SDL_GameController* controller = it->second.controller;
            for (size_t i = 0; i < state.buttons.size(); ++i) {
                state.buttons[i] = SDL_GameControllerGetButton(controller, SDL_GameControllerButton(i)) ? 1 : 0;
            }
            for (size_t i = 0; i < state.axes.size(); ++i) {
                state.axes[i] = SDL_GameControllerGetAxis(controller, SDL_GameControllerAxis(i));
            }
            return true;
        }
        SDL_Joystick* joystick = it->second.joystick;
        for (size_t i = 0; i < state.buttons.size(); ++i) {
            state.buttons[i] = SDL_JoystickGetButton(joystick, int(i)) ? 1 : 0;
        }
        for (size_t i = 0; i < state.axes.size(); ++i) {
            state.axes[i] = SDL_JoystickGetAxis(joystick, int(i));
        }
        for (size_t i = 0; i < state.hats.size(); ++i) {
            state.hats[i] = SDL_JoystickGetHat(joystick, int(i));
        }
        return true;
    }

private:
    struct Opened {
        SDL_GameController* controller = nullptr;
        SDL_Joystick* joystick = nullptr;
    };
    std::map<int64_t, Opened> opened_;
    std::vector<std::string> raw_profile_guids_;
};

// Reads RUSH_INPUT_CONFIG or the saved user profile. Missing file: defaults. Unreadable or
// invalid file: built-in defaults and the reasons in `errors`.
inline Config load_config_from_environment(std::vector<std::string>& errors) {
    const char* explicit_path = std::getenv("RUSH_INPUT_CONFIG");
    const std::string selected_path = remap_config_path();
    if (selected_path.empty()) return {};
    if (!explicit_path || !*explicit_path) {
        std::error_code status_error;
        const bool present = std::filesystem::exists(selected_path, status_error);
        if (status_error) { errors.push_back("Cannot inspect saved controller profile: " + status_error.message()); return {}; }
        if (!present) return {};
    }
    const char* path = selected_path.c_str();
    // Bound the read before allocating/parsing. On POSIX, nonblocking open
    // prevents a FIFO config from hanging startup before main is entered.
#ifdef _WIN32
    const int descriptor = _open(path, _O_RDONLY | _O_BINARY | _O_NOINHERIT);
#else
    const int descriptor = ::open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
#endif
    if (descriptor < 0) {
        errors.push_back(std::string("cannot open ") + path);
        return {};
    }
    struct OwnedDescriptor {
        int value;
        ~OwnedDescriptor() {
#ifdef _WIN32
            _close(value);
#else
            ::close(value);
#endif
        }
    } owned{descriptor};
#ifdef _WIN32
    struct _stat64 info{};
    const bool regular = _fstat64(descriptor, &info) == 0 && (info.st_mode & _S_IFMT) == _S_IFREG;
#else
    struct stat info{};
    const bool regular = ::fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode);
#endif
    if (!regular) {
        errors.push_back("configuration must be a regular file");
        return {};
    }
    constexpr size_t limit = 65536;
    if (info.st_size > static_cast<int64_t>(limit)) {
        errors.push_back("configuration larger than 64 KiB");
        return {};
    }
    std::array<char, limit + 1> buffer{};
    size_t used = 0;
    while (used < buffer.size()) {
#ifdef _WIN32
        const int count = _read(descriptor, buffer.data() + used, static_cast<unsigned>(buffer.size() - used));
#else
        const ssize_t count = ::read(descriptor, buffer.data() + used, buffer.size() - used);
#endif
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) {
            errors.push_back("configuration read failed");
            return {};
        }
        if (count == 0) break;
        used += static_cast<size_t>(count);
    }
    if (used > limit) {
        errors.push_back("configuration larger than 64 KiB");
        return {};
    }
    const std::string text(buffer.data(), used);
    ConfigResult result = parse_config(text);
    if (!result.ok()) {
        errors = result.errors;
        return {};
    }
    return std::move(result.config);
}

inline DiagnosticOptions diagnostic_options_from_environment() {
    DiagnosticOptions options;
    const char* value = std::getenv("RUSH_INPUT_DIAGNOSTIC");
    if (value && *value && std::string(value) != "0") {
        options.enabled = true;
        options.raw = std::string(value) == "raw";
    }
    return options;
}

inline constexpr uint32_t sdl_init_flags = SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER;

// Merges the keyboard with the controller manager for the runtime callbacks.
class Router {
public:
    Router(keyboard::Input& keyboard, Manager& manager) : keyboard_(keyboard), manager_(manager) {}

    void handle_event(const SDL_Event& event) {
        switch (event.type) {
        case SDL_JOYDEVICEADDED: manager_.device_added(event.jdevice.which); break;
        case SDL_JOYDEVICEREMOVED: manager_.device_removed(event.jdevice.which); break;
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP: manager_.device_changed(event.jbutton.which); break;
        case SDL_JOYAXISMOTION: manager_.device_changed(event.jaxis.which); break;
        case SDL_JOYHATMOTION: manager_.device_changed(event.jhat.which); break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: manager_.device_changed(event.cbutton.which); break;
        case SDL_CONTROLLERAXISMOTION: manager_.device_changed(event.caxis.which); break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                manager_.focus_lost();
            } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                manager_.focus_gained();
            }
            break;
        case SDL_QUIT: manager_.shutdown(); break;
        default: break;
        }
        if (manager_.keyboard_port() >= 0) {
            keyboard_.handle_event(event);
        }
    }

    bool connected(int port) const {
        if (port < 0 || port >= port_count) {
            return false;
        }
        PortState pad;
        return port == manager_.keyboard_port() || manager_.controller_state(port, pad);
    }

    // Matches the runtime's get_input contract: false means "no controller".
    bool get_input(int port, uint16_t* buttons, float* x, float* y) {
        if (!buttons || !x || !y || port < 0 || port >= port_count) {
            return false;
        }
        PortState keyboard_state;
        if (port == manager_.keyboard_port()) {
            const auto sample = keyboard_.sample();
            keyboard_state = {true, sample.buttons, float(sample.x), float(sample.y)};
        }
        PortState pad;
        const bool pad_present = manager_.controller_state(port, pad);
        if (!keyboard_state.connected && !pad_present) {
            return false;
        }
        const PortState merged = combine(keyboard_state, pad);
        *buttons = merged.buttons;
        *x = merged.x;
        *y = merged.y;
        return true;
    }

private:
    keyboard::Input& keyboard_;
    Manager& manager_;
};

// Owns the SDL backend, manager and router. Construction performs no SDL calls,
// so it is safe as a static; sdl_init_flags must be initialised before the
// event loop first pumps so already-connected devices are announced.
class System {
public:
    System(keyboard::Input& keyboard, Config config, DiagnosticOptions diagnostics, Manager::Sink sink)
        : current_config_(config), backend_(config), manager_(backend_, std::move(config), diagnostics, std::move(sink)), router_(keyboard, manager_) {}

    explicit System(keyboard::Input& keyboard)
        : System(keyboard, environment_config(), diagnostic_options_from_environment(), [](const std::string& line) {
              std::fprintf(stderr, "%s\n", line.c_str());
          }) {}

    Manager& manager() { return manager_; }
    void open_remapping(SDL_Window* parent = nullptr) {
            mapping_open_.store(true);
            struct Reset { std::atomic<bool>& flag; ~Reset() { flag.store(false); } } reset{mapping_open_};
            SDL_Event focus{};
            focus.type = SDL_WINDOWEVENT; focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
            router_.handle_event(focus);
            if (auto changed = run_input_remapping(parent, current_config_)) {
                manager_.shutdown();
                backend_.configure_profiles(*changed);
                manager_.replace_config(*changed);
                current_config_ = *changed;
                for (int i = 0; i < SDL_NumJoysticks(); ++i) manager_.device_added(i);
            }
            if (SDL_GetKeyboardFocus()) {
                focus.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
                router_.handle_event(focus);
            }
    }
    void handle_event(const SDL_Event& event) {
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F8 && !event.key.repeat) {
            open_remapping(SDL_GetKeyboardFocus());
            return;
        }
        router_.handle_event(event);
    }
    bool connected(int port) const { return router_.connected(port); }
    bool get_input(int port, uint16_t* buttons, float* x, float* y) {
        if (mapping_open_.load()) {
            if (!buttons || !x || !y) return false;
            *buttons = 0; *x = 0; *y = 0;
            return router_.connected(port);
        }
        return router_.get_input(port, buttons, x, y);
    }

private:
    static Config environment_config() {
        std::vector<std::string> errors;
        Config config = load_config_from_environment(errors);
        for (const std::string& error : errors) {
            std::fprintf(stderr, "[input] RUSH_INPUT_CONFIG rejected, using defaults: %s\n", error.c_str());
        }
        return config;
    }

    Config current_config_;
    std::atomic<bool> mapping_open_{false};
    SdlBackend backend_;
    Manager manager_;
    Router router_;
};

} // namespace rush::input
