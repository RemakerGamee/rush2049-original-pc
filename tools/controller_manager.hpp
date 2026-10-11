#pragma once

#include "input_profile.hpp"
#include <stdexcept>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Device registry, port assignment, focus handling and bounded diagnostics for
// physical controllers. No SDL dependency: a Backend supplies devices and raw
// state, so the logic is tested with a fake backend and runs against SDL in
// controller_input.hpp.
namespace rush::input {

struct DeviceInfo {
    int64_t instance = -1;     // stable for one connection; never reused as an index
    std::string name;
    std::string guid;          // 32 lowercase hex characters
    std::string serial;        // empty when the device does not report one
    bool game_controller = false;
    Capabilities caps;
};

class Backend {
public:
    enum class OpenResult { Opened, AlreadyOpen, Failed };
    virtual ~Backend() = default;
    // device_index is only meaningful at the moment of the SDL_JOYDEVICEADDED event.
    virtual OpenResult open(int device_index, DeviceInfo& info) = 0;
    virtual void close(int64_t instance) = 0;
    virtual bool read_state(int64_t instance, RawState& state) = 0;
};

enum class DeviceStatus { Mapped, PendingProfile, ProfileInvalid, NoFreePort };

inline const char* status_name(DeviceStatus status) {
    switch (status) {
    case DeviceStatus::Mapped: return "mapped";
    case DeviceStatus::PendingProfile: return "pending-profile";
    case DeviceStatus::ProfileInvalid: return "profile-invalid";
    case DeviceStatus::NoFreePort: return "no-free-port";
    }
    return "unknown";
}

struct DeviceSummary {
    DeviceInfo info;
    DeviceStatus status = DeviceStatus::PendingProfile;
    int port = -1;
    std::string profile_id;
    std::string detail; // validation error, when any
};

struct DiagnosticOptions {
    bool enabled = false;
    bool raw = false;       // also log converted-state changes
    int max_lines = 64;     // lifecycle lines
    int max_raw_lines = 200;
};

class Manager {
public:
    using Sink = std::function<void(const std::string&)>;

    Manager(Backend& backend, Config config, DiagnosticOptions diagnostics = {}, Sink sink = {})
        : backend_(backend), config_(std::move(config)), diagnostics_(diagnostics), sink_(std::move(sink)) {
        for (const auto& entry : config_.assignments) {
            reservations_[entry.first] = entry.second;
        }
        if (config_.keyboard_port < -1 || config_.keyboard_port >= port_count) {
            config_.keyboard_port = 0;
        }
    }

    int keyboard_port() const { return config_.keyboard_port; }

    void device_added(int device_index) {
        std::lock_guard<std::mutex> guard(mutex_);
        DeviceInfo info;
        const auto result = backend_.open(device_index, info);
        if (result == Backend::OpenResult::Failed) {
            log("open failed for device index " + std::to_string(device_index));
            return;
        }
        if (result == Backend::OpenResult::AlreadyOpen || find(info.instance) != devices_.end()) {
            log("ignored duplicate add for instance " + std::to_string(info.instance));
            return;
        }
        Device device;
        device.summary.info = info;
        device.key = info.guid + (info.serial.empty() ? "" : ":" + info.serial);
        select_profile(device);
        device.raw.resize(info.caps);
        devices_.push_back(std::move(device));
        Device& added = devices_.back();
        if (added.summary.status == DeviceStatus::Mapped) {
            assign_port(added);
        }
        if (focused_) {
            refresh(added);
        }
        log("added " + describe(added));
    }

    void device_removed(int64_t instance) {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto it = find(instance);
        if (it == devices_.end()) {
            return; // duplicate or unknown removal
        }
        const std::string text = describe(*it);
        backend_.close(instance);
        devices_.erase(it);
        log("removed " + text);
        for (Device& waiting : devices_) {
            if (waiting.summary.status == DeviceStatus::NoFreePort) {
                waiting.summary.status = DeviceStatus::Mapped;
                assign_port(waiting);
                if (focused_) {
                    refresh(waiting);
                }
            }
        }
    }

    // Any state event for the instance: re-read the device. Idempotent, so an
    // SDL game controller emitting both CONTROLLER and JOY events is harmless.
    void device_changed(int64_t instance) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!focused_) {
            return;
        }
        const auto it = find(instance);
        if (it != devices_.end()) {
            refresh(*it);
        }
    }

    void focus_lost() {
        std::lock_guard<std::mutex> guard(mutex_);
        focused_ = false;
        for (Device& device : devices_) {
            device.raw.neutral();
            device.state = {};
        }
        log("focus lost: controller state cleared");
    }

    void focus_gained() {
        std::lock_guard<std::mutex> guard(mutex_);
        focused_ = true;
        for (Device& device : devices_) {
            refresh(device);
        }
        log("focus gained: controller state resynchronised");
    }

    // Called by System only after its device shutdown, on the event thread.
    void replace_config(Config config) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!devices_.empty()) throw std::logic_error("Close devices before replacing controller configuration.");
        config_ = std::move(config);
    }

    void shutdown() {
        std::lock_guard<std::mutex> guard(mutex_);
        for (const Device& device : devices_) {
            backend_.close(device.summary.info.instance);
        }
        devices_.clear();
    }

    // True when a controller is assigned to this port. `state` is neutral
    // (but connected) while the window is unfocused.
    bool controller_state(int port, PortState& state) const {
        std::lock_guard<std::mutex> guard(mutex_);
        state = {};
        if (port < 0 || port >= port_count) {
            return false;
        }
        for (const Device& device : devices_) {
            if (device.summary.status == DeviceStatus::Mapped && device.summary.port == port) {
                state = device.state;
                state.connected = true;
                return true;
            }
        }
        return false;
    }

    std::vector<DeviceSummary> devices() const {
        std::lock_guard<std::mutex> guard(mutex_);
        std::vector<DeviceSummary> out;
        for (const Device& device : devices_) {
            out.push_back(device.summary);
        }
        return out;
    }

    int diagnostic_lines() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return lines_;
    }

private:
    struct Device {
        DeviceSummary summary;
        std::string key;
        Profile profile;
        RawState raw;
        PortState state;
    };

    std::vector<Device>::iterator find(int64_t instance) {
        return std::find_if(devices_.begin(), devices_.end(),
                            [&](const Device& d) { return d.summary.info.instance == instance; });
    }

    void select_profile(Device& device) {
        const DeviceInfo& info = device.summary.info;
        const Profile* chosen = nullptr;
        for (const Profile& candidate : config_.profiles) {
            if (info.game_controller ? candidate.game_controller : (!candidate.game_controller && candidate.guid == info.guid)) {
                chosen = &candidate;
                break;
            }
        }
        if (!chosen && info.game_controller) {
            device.profile = default_game_controller_profile();
            device.profile.deadzone = config_.deadzone;
            chosen = &device.profile;
        } else if (chosen) {
            device.profile = *chosen;
        }
        if (!chosen) {
            device.summary.status = DeviceStatus::PendingProfile;
            return;
        }
        device.summary.profile_id = device.profile.id;
        if (const auto error = validate_profile(device.profile, info.caps)) {
            device.summary.status = DeviceStatus::ProfileInvalid;
            device.summary.detail = *error;
            return;
        }
        device.summary.status = DeviceStatus::Mapped;
    }

    bool port_in_use(int port, const Device* except) const {
        return std::any_of(devices_.begin(), devices_.end(), [&](const Device& d) {
            return &d != except && d.summary.status == DeviceStatus::Mapped && d.summary.port == port;
        });
    }

    bool reserved_for_other(int port, const std::string& key) const {
        for (const auto& entry : reservations_) {
            if (entry.second == port && entry.first != key) {
                return true;
            }
        }
        return false;
    }

    // One controller per port. A device regains its remembered or configured
    // port on reconnect; otherwise it takes the lowest port neither in use nor
    // reserved for a different device.
    void assign_port(Device& device) {
        device.summary.port = -1;
        int wanted = -1;
        const auto remembered = reservations_.find(device.key);
        if (remembered != reservations_.end()) {
            wanted = remembered->second;
        } else if (const auto configured = reservations_.find(device.summary.info.guid); configured != reservations_.end()) {
            wanted = configured->second;
        }
        if (wanted >= 0 && !port_in_use(wanted, &device)) {
            device.summary.port = wanted;
        }
        for (int port = 0; device.summary.port < 0 && port < port_count; ++port) {
            if (!port_in_use(port, &device) && !reserved_for_other(port, device.key)) {
                device.summary.port = port;
            }
        }
        for (int port = 0; device.summary.port < 0 && port < port_count; ++port) {
            if (!port_in_use(port, &device)) {
                device.summary.port = port; // every free port is reserved: take the lowest
                for (auto it = reservations_.begin(); it != reservations_.end();) {
                    it = (it->second == port && it->first != device.key) ? reservations_.erase(it) : std::next(it);
                }
            }
        }
        if (device.summary.port < 0) {
            device.summary.status = DeviceStatus::NoFreePort;
            return;
        }
        reservations_[device.key] = device.summary.port;
    }

    void refresh(Device& device) {
        if (!backend_.read_state(device.summary.info.instance, device.raw)) {
            device.raw.neutral();
        }
        PortState converted = device.summary.status == DeviceStatus::Mapped
                                  ? convert_state(device.profile, device.raw)
                                  : PortState{};
        converted.x = sanitize_axis(converted.x);
        converted.y = sanitize_axis(converted.y);
        if (diagnostics_.enabled && diagnostics_.raw &&
            (converted.buttons != device.state.buttons || converted.x != device.state.x || converted.y != device.state.y)) {
            if (raw_lines_ < diagnostics_.max_raw_lines) {
                char text[96];
                std::snprintf(text, sizeof(text), "port %d state buttons=0x%04x x=%.2f y=%.2f", device.summary.port,
                              unsigned(converted.buttons), double(converted.x), double(converted.y));
                emit(text);
                if (++raw_lines_ == diagnostics_.max_raw_lines) {
                    emit("raw state logging limit reached");
                }
            }
        }
        device.state = converted;
    }

    std::string describe(const Device& device) const {
        const DeviceInfo& info = device.summary.info;
        std::string text = "instance " + std::to_string(info.instance) + " '" + info.name + "' guid " + info.guid +
                           (info.game_controller ? " [gamecontroller]" : " [raw joystick]") + " buttons " +
                           std::to_string(info.caps.buttons) + " axes " + std::to_string(info.caps.axes) +
                           " hats " + std::to_string(info.caps.hats) + " status " + status_name(device.summary.status);
        if (device.summary.port >= 0) {
            text += " port " + std::to_string(device.summary.port);
        }
        if (!device.summary.profile_id.empty()) {
            text += " profile " + device.summary.profile_id;
        }
        if (!device.summary.detail.empty()) {
            text += " (" + device.summary.detail + ")";
        }
        return text;
    }

    void emit(const std::string& line) {
        if (sink_) {
            sink_("[input] " + line);
        }
    }

    void log(const std::string& line) {
        if (!diagnostics_.enabled || lines_ >= diagnostics_.max_lines) {
            return;
        }
        emit(line);
        if (++lines_ == diagnostics_.max_lines) {
            emit("diagnostic line limit reached");
        }
    }

    mutable std::mutex mutex_;
    Backend& backend_;
    Config config_;
    DiagnosticOptions diagnostics_;
    Sink sink_;
    std::vector<Device> devices_;
    std::map<std::string, int> reservations_; // device key (guid[:serial]) or configured guid -> port
    bool focused_ = true;
    int lines_ = 0;
    int raw_lines_ = 0;
};

} // namespace rush::input
