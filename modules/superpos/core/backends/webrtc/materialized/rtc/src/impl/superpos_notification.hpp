// Copyright (c) 2026 Superpos contributors.
// SPDX-License-Identifier: MIT
#pragma once
#include <usrsctp.h>
#include <span>
#include <cstddef>
#include <cstring>
#include <cstdint>
namespace rtc::impl {
// Notifications use the local usrsctp ABI, never the application wire codec.
// Validate before interpreting union bodies or subtracting flexible-array sizes.
inline bool superposNotificationValid(std::span<const std::byte> bytes) noexcept {
    using Header = decltype(sctp_notification{}.sn_header);
    if (bytes.size() < sizeof(Header) || bytes.size() > 4096) return false;
    Header header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.sn_length != bytes.size()) return false;
    switch (header.sn_type) {
    case SCTP_ASSOC_CHANGE:
        return bytes.size() >= sizeof(sctp_assoc_change);
    case SCTP_SENDER_DRY_EVENT:
        return bytes.size() == sizeof(sctp_sender_dry_event);
    case SCTP_STREAM_RESET_EVENT:
        return bytes.size() >= sizeof(sctp_stream_reset_event) &&
               (bytes.size() - sizeof(sctp_stream_reset_event)) % sizeof(std::uint16_t) == 0;
    default:
        return true; // Unsubscribed bodies remain ignored, never interpreted.
    }
}
}
