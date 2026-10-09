// Copyright (c) 2026 Superpos contributors. SPDX-License-Identifier: MIT
#pragma once
#include <rtc/description.hpp>
#include <rtc/peerconnection.hpp>
#include <atomic>
#include <exception>
#ifdef _MSC_VER
// Debug CRT stays selected; the STL container ABI is identical across this
// whole private profile. MSVC's debug-proxy string move may allocate in a
// noexcept constructor, so it cannot support our allocation-failure boundary.
static_assert(_ITERATOR_DEBUG_LEVEL==0);
#endif

namespace superpos::rtc_profile {
enum class Error { Unsupported, Full, Contended, Closed };
enum class ChannelState { Empty, Constructing, Published, Sealed };
static_assert(std::atomic<ChannelState>::is_always_lock_free);
class Failure final : public std::exception {
    Error error_;
public:
    explicit Failure(Error error) noexcept : error_(error) {}
    Error code() const noexcept { return error_; }
    const char* what() const noexcept override { return "SUPERPOS_PROFILE_ADMISSION_REJECTED"; }
};
inline bool channel_allowed(const rtc::string& label, const rtc::DataChannelInit& init) noexcept {
    return init.negotiated && init.id && *init.id == 0 && init.reliability.unordered
        && init.reliability.maxRetransmits && *init.reliability.maxRetransmits == 0
        && !init.reliability.maxPacketLifeTime
        && init.reliability.typeDeprecated == rtc::Reliability::Type::Reliable
        && label.size() <= 64 && init.protocol.size() <= 64;
}
inline bool description_allowed(const rtc::Description& description) {
    // Removed/inactive Media entries still allocate tracks in the generic path.
    // Inspect variant alternatives, never hasAudioOrVideo or copied type strings.
    if (description.type() == rtc::Description::Type::Rollback)
        return description.mediaCount() == 0;
    if (description.mediaCount() != 1) return false;
    const auto entry = description.media(0);
    const auto* application = std::get_if<const rtc::Description::Application*>(&entry);
    return application && *application && !(*application)->isRemoved()
        && (*application)->direction() != rtc::Description::Direction::Inactive;
}
inline void require_description(const rtc::Description& description) {
    if (!description_allowed(description)) throw Failure(Error::Unsupported);
}
}
