// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace superpos_egp {
// A defined C++17 conversion, including UINT64_MAX, without implementation-defined
// unsigned-to-signed casts. Variant/GDScript carries this opaque bit pattern.
inline int64_t signed_bits(uint64_t value) noexcept {
    return value <= uint64_t(INT64_MAX) ? int64_t(value) : -1 - int64_t(UINT64_MAX - value);
}
inline uint64_t unsigned_bits(int64_t value) noexcept { return uint64_t(value); }
inline bool parse_decimal(const std::string &text, uint64_t &value) noexcept {
    if (text.empty() || text.size() > 20 || (text.size() > 1 && text[0] == '0')) {
        return false;
    }
    uint64_t candidate = 0;
    for (char digit : text) {
        if (digit < '0' || digit > '9') { return false; }
        uint64_t n = uint64_t(digit - '0');
        if (candidate > (UINT64_MAX - n) / 10) { return false; }
        candidate = candidate * 10 + n;
    }
    value = candidate;
    return true;
}
inline int compare(uint64_t a, uint64_t b) noexcept { return a < b ? -1 : a > b ? 1 : 0; }
inline bool checked_add(uint64_t a, uint64_t b, uint64_t &out) noexcept {
    if (b > UINT64_MAX - a) { return false; }
    out = a + b;
    return true;
}
}
