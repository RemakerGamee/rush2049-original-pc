#pragma once
#include <cstdint>
namespace rush_pak {
struct Storage {
    virtual ~Storage() = default;
    virtual bool read_block(uint16_t address, uint8_t* data) const = 0;
    virtual bool write_block(uint16_t address, const uint8_t* data) = 0;
};
}
