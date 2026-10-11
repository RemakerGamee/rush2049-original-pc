#pragma once
#include "input_profile.hpp"
#include <SDL.h>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
#include <sys/stat.h>
#include <locale>
#include <fcntl.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace rush::input {
inline std::string remap_config_path() {
    if (const char* p = std::getenv("RUSH_INPUT_CONFIG"); p && *p) return p;
#ifdef _WIN32
    if (const char* p = std::getenv("LOCALAPPDATA"); p && *p)
        return (std::filesystem::path(p) / "Rush2049" / "input.cfg").string();
#else
    if (const char* p = std::getenv("XDG_CONFIG_HOME"); p && *p)
        return (std::filesystem::path(p) / "rush2049" / "input.cfg").string();
    if (const char* p = std::getenv("HOME"); p && *p)
        return (std::filesystem::path(p) / ".config" / "rush2049" / "input.cfg").string();
#endif
    return {};
}
inline std::string serialize_input_config(const Config& c) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "rush-input 1\nkeyboard-port ";
    if (c.keyboard_port < 0) out << "none\n"; else out << c.keyboard_port << '\n';
    out << "deadzone " << c.deadzone << '\n';
    for (const auto& [guid, port] : c.assignments) out << "assign " << guid << ' ' << port << '\n';
    for (const auto& p : c.profiles) {
        out << "profile " << p.id << (p.game_controller ? " gamecontroller\n" : " guid " + p.guid + "\n");
        out << "deadzone " << p.deadzone << '\n';
        for (int i = 0; i < 2; ++i) {
            const auto& a = i ? p.stick_y : p.stick_x;
            if (a.set) out << (i ? "stick-y axis " : "stick-x axis ") << a.axis << (a.invert ? " invert\n" : "\n");
        }
        for (size_t i = 0; i < button_count; ++i) for (const auto& b : p.bindings[i]) {
            out << "bind " << button_name(Button(i)) << ' ';
            if (b.kind == BindKind::Button) out << "button " << b.index;
            if (b.kind == BindKind::Axis) out << "axis " << b.index << (b.axis_sign < 0 ? " - " : " + ") << b.threshold;
            if (b.kind == BindKind::Hat) out << "hat " << b.index << ' ' <<
                (b.hat_mask == hat_up ? "up" : b.hat_mask == hat_down ? "down" : b.hat_mask == hat_left ? "left" : "right");
            out << '\n';
        }
    }
    return out.str();
}
// Validated complete config, exclusive temporary file, retained previous config,
// atomic replacement. Never writes a save/Pak or overwrites its backup.
inline bool save_remap_config(const Config& config, const std::string& destination, std::string& error) {
    namespace fs = std::filesystem;
    const std::string text = serialize_input_config(config);
    if (text.size() > 65536 || !parse_config(text).ok() || destination.empty()) {
        error = "Invalid configuration or unavailable profile destination."; return false;
    }
    fs::path tmp;
    int fd = -1;
    try {
        fs::path target(destination);
        if (fs::is_symlink(target) || (fs::exists(target) && !fs::is_regular_file(target)))
            throw std::runtime_error("Profile destination must be a regular file.");
        if (!target.parent_path().empty()) fs::create_directories(target.parent_path());
        static unsigned counter = 0;
        for (int attempt = 0; attempt < 32 && fd < 0; ++attempt) {
            tmp = target.string() + ".new-" + std::to_string(SDL_GetTicks64()) + "-" + std::to_string(++counter);
#ifdef _WIN32
            fd = _wopen(tmp.wstring().c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
            fd = ::open(tmp.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
#endif
        }
        if (fd < 0) throw std::runtime_error("Cannot create exclusive profile temporary file.");
        size_t offset = 0;
        while (offset < text.size()) {
#ifdef _WIN32
            int count = _write(fd, text.data() + offset, unsigned(text.size() - offset));
#else
            auto count = ::write(fd, text.data() + offset, text.size() - offset);
#endif
            if (count <= 0) throw std::runtime_error("Profile write failed.");
            offset += size_t(count);
        }
#ifdef _WIN32
        if (_commit(fd) != 0) throw std::runtime_error("Profile flush failed.");
        _close(fd);
#else
        if (::fsync(fd) != 0) throw std::runtime_error("Profile flush failed.");
        ::close(fd);
#endif
        fd = -1;
        if (fs::exists(target)) fs::copy_file(target, tmp.string() + ".previous", fs::copy_options::none);
#ifdef _WIN32
        if (!MoveFileExW(tmp.wstring().c_str(), target.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Profile replacement failed; previous configuration preserved.");
#else
        fs::rename(tmp, target);
#endif
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        if (fd >= 0) {
#ifdef _WIN32
            _close(fd);
#else
            ::close(fd);
#endif
        }
        std::error_code ignored;
        if (!tmp.empty()) fs::remove(tmp, ignored);
        return false;
    }
}
inline int remap_dialog(SDL_Window* parent, const char* title, const std::string& text,
                        const std::vector<std::string>& choices) {
    std::vector<SDL_MessageBoxButtonData> buttons;
    for (size_t i = 0; i < choices.size(); ++i) buttons.push_back({0, int(i), choices[i].c_str()});
    SDL_MessageBoxData data{SDL_MESSAGEBOX_INFORMATION, parent, title, text.c_str(), int(buttons.size()), buttons.data(), nullptr};
    int selected = -1;
    if (SDL_ShowMessageBox(&data, &selected) != 0) { std::fprintf(stderr, "Controller mapping dialog: %s\n", SDL_GetError()); return -1; }
    return selected;
}
// UI belongs to the port's System; physical events always use raw SDL indices.
#ifdef _WIN32
#include "windows_input_ui.hpp"
#endif
inline std::optional<Config> run_input_remapping(SDL_Window* parent, const Config& original) {
#ifdef _WIN32
    NativeInputWindow ui;return ui.run(original);
#else
    if (SDL_NumJoysticks() <= 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Controller setup", "Connect a controller, then press F8 again.", parent);
        return {};
    }
    std::vector<std::string> names;
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        names.push_back(std::string(SDL_JoystickNameForIndex(i) ? SDL_JoystickNameForIndex(i) : "Controller") + " #" + std::to_string(i + 1));
    names.push_back("Cancel");
    int selected = remap_dialog(parent, "Controller setup", "Choose the physical controller to remap.", names);
    if (selected < 0 || selected >= SDL_NumJoysticks()) return {};
    SDL_Joystick* joystick = SDL_JoystickOpen(selected);
    if (!joystick) return {};
    struct Close { SDL_Joystick* j; ~Close() { SDL_JoystickClose(j); } } close{joystick};
    char guid[33]{};
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), guid, sizeof(guid));
    Config changed = original;
    Profile profile;
    profile.id = std::string("pad-") + guid; profile.guid = guid; profile.deadzone = original.deadzone;
    for (const auto& existing : original.profiles) if (!existing.game_controller && existing.guid == guid) profile = existing;
    const int preset=remap_dialog(parent,"Controller preset","Choose a starting layout. Every button and both stick axes can be changed afterward. N64 USB layouts vary by adapter.",
        {"Custom / current","N64 KIWITATA","N64 SAFFUN","Xbox One / SDL","Cancel"});
    if(preset<0 || preset==4)return {};
    if(preset==1 || preset==2){
        profile.bindings={};profile.stick_x={true,0,false};profile.stick_y={true,1,true};
        const int buttons[]={2,1,preset==1?12:9,6,4,5,-1,-1,-1,-1,preset==1?9:11,3,0,preset==1?8:10};
        for(size_t i=0;i<button_count;++i)if(buttons[i]>=0)profile.bindings[i]={{BindKind::Button,buttons[i]}};
        const uint8_t masks[]={1,4,8,2};for(int i=0;i<4;++i)profile.bindings[6+i]={{BindKind::Hat,0,masks[i]}};
    }
    if(preset==3){
        SDL_GameController* controller=SDL_IsGameController(selected)?SDL_GameControllerOpen(selected):nullptr;
        if(!controller){SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,"Xbox preset unavailable","SDL does not recognise this controller's Xbox layout. Use Custom to map each control.",parent);}
        else {
            profile=default_game_controller_profile();profile.id=std::string("pad-")+guid;profile.guid=guid;profile.game_controller=false;
            for(auto& bindings:profile.bindings)for(auto& binding:bindings){
                const auto raw=binding.kind==BindKind::Button?SDL_GameControllerGetBindForButton(controller,SDL_GameControllerButton(binding.index)):SDL_GameControllerGetBindForAxis(controller,SDL_GameControllerAxis(binding.index));
                if(raw.bindType==SDL_CONTROLLER_BINDTYPE_BUTTON){binding.kind=BindKind::Button;binding.index=raw.value.button;}
                else if(raw.bindType==SDL_CONTROLLER_BINDTYPE_AXIS){binding.kind=BindKind::Axis;binding.index=raw.value.axis;}
                else if(raw.bindType==SDL_CONTROLLER_BINDTYPE_HAT){binding.kind=BindKind::Hat;binding.index=raw.value.hat.hat;binding.hat_mask=raw.value.hat.hat_mask;}
                else {binding.index=-1;}
            }
            for(auto axis:{&profile.stick_x,&profile.stick_y}){const auto raw=SDL_GameControllerGetBindForAxis(controller,SDL_GameControllerAxis(axis->axis));axis->set=raw.bindType==SDL_CONTROLLER_BINDTYPE_AXIS;if(axis->set)axis->axis=raw.value.axis;}
            SDL_GameControllerClose(controller);
        }
    }
    int page = 0;
    while (true) {
        std::vector<std::string> choices;
        std::string text = "Select an N64 control, then press its physical button.\nStick X: move RIGHT. Stick Y: move UP.\n";
        for (int i = page * 4; i < (page + 1) * 4; ++i) {
            std::string name = i < int(button_count) ? button_name(Button(i)) : i == 14 ? "Stick X" : "Stick Y";
            choices.push_back(name);
            text += name + ": ";
            if (i < int(button_count)) {
                const auto& bs = profile.bindings[size_t(i)];
                text += bs.empty() ? "unassigned" : bs[0].kind == BindKind::Button ? "button " + std::to_string(bs[0].index) :
                    bs[0].kind == BindKind::Hat ? "hat " + std::to_string(bs[0].index) + " mask " + std::to_string(bs[0].hat_mask) :
                    "axis " + std::to_string(bs[0].index);
            } else {
                const auto& a = i == 14 ? profile.stick_x : profile.stick_y;
                text += a.set ? "axis " + std::to_string(a.axis) + (a.invert ? " inverted" : "") : "unassigned";
            }
            text += "\n";
        }
        choices.insert(choices.end(), {"Next page", "Save", "Cancel"});
        int action = remap_dialog(parent, "Rush 2049 controller mapping", text, choices);
        if (!SDL_JoystickGetAttached(joystick)) return {};
        if (action < 0 || action == 6) return {};
        if (action == 4) { page = (page + 1) % 4; continue; }
        if (action == 5) {
            Capabilities caps{SDL_JoystickNumButtons(joystick), SDL_JoystickNumAxes(joystick), SDL_JoystickNumHats(joystick)};
            auto error = validate_profile(profile, caps);
            if (error) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Mapping not saved", error->c_str(), parent); continue; }
            auto it = std::find_if(changed.profiles.begin(), changed.profiles.end(), [&](const Profile& p) { return !p.game_controller && p.guid == profile.guid; });
            if (it == changed.profiles.end()) changed.profiles.push_back(profile); else *it = profile;
            std::string message;
            if (!save_remap_config(changed, remap_config_path(), message)) {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Mapping not saved", message.c_str(), parent); continue;
            }
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Mapping saved", "Your controller mapping is saved. Other device profiles are preserved.", parent);
            return changed;
        }
        int target = page * 4 + action;
        std::string prompt = target < int(button_count) ? std::string("Press the physical control for ") + button_name(Button(target)) :
            target == 14 ? "Move the stick fully RIGHT." : "Move the stick fully UP.";
        prompt += "\nAfter OK you have 10 seconds. Escape cancels.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Assign control", prompt.c_str(), parent);
        SDL_PumpEvents();
        SDL_FlushEvent(SDL_JOYBUTTONDOWN);
        SDL_FlushEvent(SDL_JOYHATMOTION);
        SDL_FlushEvent(SDL_JOYAXISMOTION);
        std::vector<int16_t> baseline(size_t(SDL_JoystickNumAxes(joystick)));
        for (size_t i = 0; i < baseline.size(); ++i) baseline[i] = SDL_JoystickGetAxis(joystick, int(i));
        Uint64 deadline = SDL_GetTicks64() + 10000;
        bool captured = false;
        SDL_Event event;
        while (SDL_GetTicks64() < deadline && SDL_JoystickGetAttached(joystick)) {
            if (!SDL_WaitEventTimeout(&event, 50)) continue;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) break;
            Binding binding;
            bool binding_ready = false;
            if (target < int(button_count) && event.type == SDL_JOYBUTTONDOWN && event.jbutton.which == SDL_JoystickInstanceID(joystick)) {
                binding = {BindKind::Button, int(event.jbutton.button)}; binding_ready = true;
            }
            if (target < int(button_count) && event.type == SDL_JOYHATMOTION && event.jhat.which == SDL_JoystickInstanceID(joystick) &&
                (event.jhat.value == 1 || event.jhat.value == 2 || event.jhat.value == 4 || event.jhat.value == 8)) {
                binding = {BindKind::Hat, int(event.jhat.hat), event.jhat.value}; binding_ready = true;
            }
            if (event.type == SDL_JOYAXISMOTION && event.jaxis.which == SDL_JoystickInstanceID(joystick) &&
                size_t(event.jaxis.axis) < baseline.size() && std::abs(int(event.jaxis.value) - int(baseline[event.jaxis.axis])) >= 16384) {
                if (target >= int(button_count)) {
                    auto& a = target == 14 ? profile.stick_x : profile.stick_y;
                    a = {true, int(event.jaxis.axis), target == 14 ? event.jaxis.value < 0 : event.jaxis.value > 0};
                    captured = true; break;
                }
                binding = {BindKind::Axis, int(event.jaxis.axis), 0, event.jaxis.value < 0 ? -1 : 1, 0.5f};
                binding_ready = true;
            }
            if (binding_ready) { profile.bindings[size_t(target)] = {binding}; captured = true; break; }
        }
        if (!captured) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "No assignment", "No control captured. Previous assignment retained.", parent);
        // Require a fresh press for the next assignment.
        Uint64 release_deadline = SDL_GetTicks64() + 3000;
        SDL_Event released;
        while (SDL_GetTicks64() < release_deadline) {
            bool held = false;
            SDL_PumpEvents();
            for (int i = 0; i < SDL_JoystickNumButtons(joystick); ++i) held |= SDL_JoystickGetButton(joystick, i) != 0;
            for (int i = 0; i < SDL_JoystickNumHats(joystick); ++i) held |= SDL_JoystickGetHat(joystick, i) != 0;
            if (!held) break;
            SDL_WaitEventTimeout(&released, 50);
        }
    }
#endif
}
}
