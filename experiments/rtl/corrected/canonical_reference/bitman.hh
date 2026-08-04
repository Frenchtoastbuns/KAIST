#pragma once
#include <cstdint>
namespace CODE {
inline bool get_be_bit(const uint8_t* data, int position) {
    return (data[position >> 3] >> (7 - (position & 7))) & 1u;
}
inline void set_be_bit(uint8_t* data, int position, bool value) {
    const uint8_t mask = static_cast<uint8_t>(1u << (7 - (position & 7)));
    if (value) data[position >> 3] |= mask;
    else data[position >> 3] &= static_cast<uint8_t>(~mask);
}
}
