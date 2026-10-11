// Opt-in controller discovery probe. It never starts the game and never reads
// the keyboard or the game. Default mode initialises joystick subsystems only;
// --window adds an isolated visible SDL window for real focus-event checks.
//
//   controller_input_probe [--seconds N] [--watch] [--window] [--remap]
//
// --watch additionally prints raw button/axis/hat indices as they change, which
// is how a mapping profile for an unrecognised pad is written (bounded output).
// Honours RUSH_INPUT_CONFIG so profile selection and port assignment can be
// previewed. Build with: g++ -std=c++20 $(pkg-config --cflags --libs sdl2)
#include "controller_input.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

using namespace rush::input;

namespace {

bool running_under_wsl() {
    std::ifstream version("/proc/version");
    std::string text((std::istreambuf_iterator<char>(version)), std::istreambuf_iterator<char>());
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text.find("microsoft") != std::string::npos;
}

} // namespace

int main(int argc, char** argv) {
    int seconds = 10;
    bool watch = false;
    bool windowed = false;
    bool remap = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--remap")) {
            remap = true; windowed = true;
        } else if (!std::strcmp(argv[i], "--window")) {
            windowed = true;
        } else if (!std::strcmp(argv[i], "--watch")) {
            watch = true;
        } else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) {
            char* end = nullptr;
            const long value = std::strtol(argv[++i], &end, 10);
            if (*end || value < 0 || value > 300) {
                std::fprintf(stderr, "--seconds needs 0-300\n");
                return 2;
            }
            seconds = int(value);
        } else {
            std::fprintf(stderr, "usage: %s [--seconds N] [--watch] [--window] [--remap]\n", argv[0]);
            return 2;
        }
    }

    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(sdl_init_flags | (windowed ? SDL_INIT_VIDEO : 0)) != 0) {
        std::fprintf(stderr, "SDL joystick initialisation failed: %s\n", SDL_GetError());
        return 3;
    }
    SDL_Window* window = windowed ? SDL_CreateWindow("Rush 2049 controller probe - no game - focus test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 240, SDL_WINDOW_SHOWN) : nullptr;
    if (windowed && !window) { std::fprintf(stderr, "probe window failed: %s\n", SDL_GetError()); SDL_Quit(); return 3; }
    SDL_version linked;
    SDL_GetVersion(&linked);
    std::printf("probe: SDL %d.%d.%d, window of %d s, watch=%s\n", linked.major, linked.minor, linked.patch, seconds,
                watch ? "on" : "off");

    std::vector<std::string> errors;
    Config config = load_config_from_environment(errors);
    for (const auto& error : errors) {
        std::printf("probe: RUSH_INPUT_CONFIG rejected, using defaults: %s\n", error.c_str());
    }
    DiagnosticOptions options;
    options.enabled = true;
    options.raw = watch;
    rush::keyboard::Input keyboard;
    System system(keyboard, config, options, [](const std::string& line) { std::printf("t=%u %s\n", unsigned(SDL_GetTicks()), line.c_str()); });
    Manager& manager = system.manager();
    if (remap) system.open_remapping(window);

    int watched_lines = 0;
    constexpr int max_watched_lines = 200;
    std::map<std::pair<int, int>, int> last_axis;
    const auto emit = [&](const char* format, auto... values) {
        if (watched_lines < max_watched_lines) {
            std::printf("t=%u ", unsigned(SDL_GetTicks()));
            std::printf(format, values...);
            if (++watched_lines == max_watched_lines) {
                std::puts("probe: watch output limit reached");
            }
        }
    };

    bool closed = false;
    const Uint32 end = SDL_GetTicks() + Uint32(seconds) * 1000u;
    int announced = 0;
    Uint32 last_sample = 0;
    SDL_Event event;
    while (!closed && int(end - SDL_GetTicks()) > 0) {
        const bool has_event = SDL_WaitEventTimeout(&event, 100) != 0;
        if (has_event && event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F8) {
            system.handle_event(event);
            continue;
        }
        if (has_event) switch (event.type) {
        case SDL_QUIT: closed = true; break;
        case SDL_WINDOWEVENT:
            if (window && event.window.windowID == SDL_GetWindowID(window)) {
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) closed = true;
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) manager.focus_lost();
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) manager.focus_gained();
            }
            break;
        case SDL_JOYDEVICEADDED:
            ++announced;
            manager.device_added(event.jdevice.which);
            if (auto* joystick = SDL_JoystickFromInstanceID(SDL_JoystickGetDeviceInstanceID(event.jdevice.which))) {
                emit("probe: physical capabilities buttons=%d axes=%d hats=%d\n", SDL_JoystickNumButtons(joystick),
                     SDL_JoystickNumAxes(joystick), SDL_JoystickNumHats(joystick));
                for (int axis = 0; axis < SDL_JoystickNumAxes(joystick); ++axis)
                    emit("probe: initial axis %d value %d\n", axis, int(SDL_JoystickGetAxis(joystick, axis)));
            }
            break;
        case SDL_JOYDEVICEREMOVED: manager.device_removed(event.jdevice.which); break;
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
            manager.device_changed(event.jbutton.which);
            if (watch) emit("probe: instance %d button %d %s\n", int(event.jbutton.which), int(event.jbutton.button), event.jbutton.state ? "down" : "up");
            break;
        case SDL_JOYAXISMOTION: {
            manager.device_changed(event.jaxis.which);
            const std::pair<int, int> key{int(event.jaxis.which), int(event.jaxis.axis)};
            if (watch && std::abs(int(event.jaxis.value) - last_axis[key]) >= 4096) {
                last_axis[key] = event.jaxis.value;
                emit("probe: instance %d axis %d value %d\n", key.first, key.second, int(event.jaxis.value));
            }
            break;
        }
        case SDL_JOYHATMOTION:
            manager.device_changed(event.jhat.which);
            if (watch) emit("probe: instance %d hat %d mask %d\n", int(event.jhat.which), int(event.jhat.hat), int(event.jhat.value));
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: manager.device_changed(event.cbutton.which); break;
        case SDL_CONTROLLERAXISMOTION: manager.device_changed(event.caxis.which); break;
        default: break;
        }
        if (watch && SDL_GetTicks() - last_sample >= 1000) {
            last_sample = SDL_GetTicks();
            for (const auto& device : manager.devices()) manager.device_changed(device.info.instance);
            for (int port = 0; port < port_count; ++port) {
                PortState state;
                const bool connected = manager.controller_state(port, state);
                if (connected || port == 0)
                    std::printf("t=%u sample port=%d connected=%d buttons=0x%04x x=%.5f y=%.5f\n",
                                unsigned(last_sample), port, int(connected), unsigned(state.buttons), double(state.x), double(state.y));
            }
            for (const auto& device : manager.devices()) {
                if (auto* joystick = SDL_JoystickFromInstanceID(SDL_JoystickID(device.info.instance))) {
                    for (int axis = 0; axis < SDL_JoystickNumAxes(joystick); ++axis)
                        std::printf("t=%u raw-sample instance=%lld axis=%d value=%d\n", unsigned(last_sample),
                                    static_cast<long long>(device.info.instance), axis, int(SDL_JoystickGetAxis(joystick, axis)));
                }
            }
        }
    }

    const auto devices = manager.devices();
    std::printf("probe: add announcements %d, devices currently open %zu, SDL_NumJoysticks %d\n", announced, devices.size(),
                SDL_NumJoysticks());
    for (const auto& device : devices) {
        std::printf("probe: device instance %lld '%s' guid %s serial %s path %s port %d status %s profile %s\n",
                    static_cast<long long>(device.info.instance), device.info.name.c_str(), device.info.guid.c_str(),
                    device.info.serial.empty() ? "-" : "reported", device.info.game_controller ? "gamecontroller" : "raw-joystick",
                    device.port, status_name(device.status), device.profile_id.empty() ? "-" : device.profile_id.c_str());
    }
    if (devices.empty()) {
        std::puts("probe: no joystick or game controller is visible to this process.");
        if (running_under_wsl()) {
            std::puts("probe: this is WSL, which does not expose Windows USB/HID game controllers to Linux SDL by default.");
            std::puts("probe: that is an environment limit, not evidence the pad is unsupported; run the native Windows build to test the pad.");
        } else {
            std::puts("probe: check that the pad is connected and readable (udev permissions) and that SDL_JOYSTICK_DEVICE is not filtering it.");
        }
    }
    manager.shutdown();
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
