#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
#include "pak_storage.hpp"

// Joybus packet framing and CRC: https://libdragon.dev/ref/group__joybus.html
// Optional block storage lets the original guest own the Controller Pak filesystem.
namespace rush_si {
using Packet = std::array<uint8_t, 64>;
struct Port {
    bool connected = false, rumble = false;
    rush_pak::Storage* memory = nullptr;
    uint16_t buttons = 0;
    int8_t x = 0, y = 0;
};
struct State { std::array<uint8_t, 4> bank{}; };
inline uint8_t crc(const uint8_t* data) {
    uint8_t result = 0;
    for (unsigned bit = 0; bit < 264; ++bit) {
        const bool top = (result & 0x80) != 0;
        result = uint8_t(result << 1);
        if (bit < 256) result |= (data[bit / 8] >> (7 - bit % 8)) & 1;
        if (top) result ^= 0x85;
    }
    return result;
}
// Only framing is inspected here; avoid treating response padding as commands.
inline bool needs_poll(const Packet& p) {
    for (unsigned i = 0; i < 63;) {
        const unsigned tx = p[i];
        if (tx == 0xFE) break;
        if (tx == 0 || tx == 0xFF || tx == 0xFD) { ++i; continue; }
        if (i + 2 >= 63 || tx > 63) break;
        const unsigned rx = p[i + 1] & 63;
        if (i + 2 + tx + rx > 63) break;
        if (tx == 1 && rx == 4 && p[i + 2] == 1) return true;
        i += 2 + tx + rx;
    }
    return false;
}
template<class Rumble>
Packet execute(const Packet& request, const std::array<Port, 4>& ports,
               State& state, Rumble set_rumble) {
    Packet reply = request;
    unsigned channel = 0;
    for (unsigned i = 0; i < 63;) {
        const unsigned tx = request[i];
        if (tx == 0xFE) break;
        if (tx == 0xFF) { ++i; continue; }
        if (tx == 0 || tx == 0xFD) { ++channel; ++i; continue; }
        if (i + 2 >= 63) break;
        const unsigned rx = request[i + 1] & 63;
        reply[i + 1] = uint8_t(rx);
        if (tx > 63 || i + 2 + tx + rx > 63) {
            reply[i + 1] |= 0x40;
            break;
        }
        const unsigned out = i + 2 + tx;
        std::fill_n(reply.begin() + out, rx, uint8_t(0));
        if (channel >= 4 || !ports[channel].connected) {
            reply[i + 1] |= 0x80;
        } else {
            const auto& port = ports[channel];
            const auto command = request[i + 2];
            if ((command == 0 || command == 0xFF) && tx == 1 && rx == 3) {
                reply[out] = 0x05; reply[out + 1] = 0;
                reply[out + 2] = (port.memory || port.rumble) ? 1 : 2;
            } else if (command == 1 && tx == 1 && rx == 4) {
                reply[out] = uint8_t(port.buttons >> 8);
                reply[out + 1] = uint8_t(port.buttons);
                reply[out + 2] = uint8_t(port.x);
                reply[out + 3] = uint8_t(port.y);
            } else if (command == 2 && tx == 3 && rx == 33) {
                const unsigned address = (request[i + 3] << 8 | request[i + 4]) & 0xFFE0;
                bool success = port.rumble;
                if (port.memory) {
                    // A standard 32 KiB Pak has one bank; its bank-select region
                    // reads zero, which distinguishes it from the Rumble Pak.
                    success = address == 0x8000 || port.memory->read_block(uint16_t(address), reply.data() + out);
                } else if (port.rumble && address >= 0x8000 && address < 0x9000 && state.bank[channel] == 0x80)
                    std::fill_n(reply.begin() + out, 32, uint8_t(0x80));
                const uint8_t error = port.memory ? (success ? 0 : 1) : (port.rumble ? 0 : 0xFF);
                reply[out + 32] = crc(reply.data() + out) ^ error;
            } else if (command == 3 && tx == 35 && rx == 1) {
                const unsigned address = (request[i + 3] << 8 | request[i + 4]) & 0xFFE0;
                bool success = port.rumble;
                if (port.memory) {
                    success = address == 0x8000 || port.memory->write_block(uint16_t(address), request.data() + i + 5);
                } else if (port.rumble) {
                    if (address == 0x8000) state.bank[channel] = request[i + 5];
                    if (address >= 0xC000) set_rumble(channel, request[i + 5] != 0);
                }
                const uint8_t error = port.memory ? (success ? 0 : 1) : (port.rumble ? 0 : 0xFF);
                reply[out] = crc(request.data() + i + 5) ^ error;
            } else {
                reply[i + 1] |= 0x40;
            }
        }
        ++channel;
        i += 2 + tx + rx;
    }
    reply[63] = 0;
    return reply;
}
}
