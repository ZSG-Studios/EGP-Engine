// Copyright (c) 2026 Superpos contributors.
// SPDX-License-Identifier: MIT
#pragma once
#include "message.hpp"
#include <exception>

namespace rtc::impl {
// An allocation-free rejection is distinct from libdatachannel's false return,
// which means the complete message was accepted into a retained send queue.
class SuperposSendRejected final : public std::exception {
public:
    enum class Reason { Frame, Channel, Reliability, Full, Closed, Contended, Counter };
    const Reason reason;
    explicit SuperposSendRejected(Reason value) noexcept:reason(value) {}
    const char* what() const noexcept override { return "Superpos SCTP send admission rejected"; }
};
// A native write may already have reached its recipient. This is terminal
// uncertainty, never a pre-admission rejection eligible for blind retry.
class SuperposSendUncertain final : public std::exception {
public:
    const char* what() const noexcept override { return "Superpos SCTP write result was not a complete frame"; }
};
inline constexpr std::size_t superposSendSlots=32;
inline constexpr std::size_t superposSendBytes=32*1024;
inline constexpr std::size_t superposSendFrame=960;
inline constexpr std::size_t superposSendQuantum=16;

inline void superposValidateSend(const message_ptr& message) {
    if(!message || message->empty() || message->size()>superposSendFrame || message->capacity()>superposSendFrame)
        throw SuperposSendRejected(SuperposSendRejected::Reason::Frame);
    if(message->stream!=0 || message->type!=Message::Binary)
        throw SuperposSendRejected(SuperposSendRejected::Reason::Channel);
    const auto& reliability=message->reliability;
    if(!reliability || !reliability->unordered || !reliability->maxRetransmits
        || *reliability->maxRetransmits!=0 || reliability->maxPacketLifeTime)
        throw SuperposSendRejected(SuperposSendRejected::Reason::Reliability);
}
struct SuperposSendSnapshot {
    std::size_t messages{},retained_bytes{},buffered_bytes{},descriptor_bytes{};
    bool closing{},reset_pending{},callback_failed{};
};
}
