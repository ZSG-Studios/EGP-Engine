// Copyright (c) 2026 Superpos contributors.
// SPDX-License-Identifier: MIT
#pragma once
#include "message.hpp"
namespace rtc::impl {
// Queue byte limits charge retained payload capacity, even for Control/Reset.
// Fixed queue descriptors, Message objects/control blocks and reliability
// ownership remain separate backend-ledger entries, not part of this value.
inline std::size_t superposPayloadCharge(const message_ptr &message) noexcept {
    return message ? message->capacity() : 0;
}
inline bool superposEncryptedIngressAdmits(const message_ptr &message) noexcept {
    return message && message->size() <= 4096 && message->capacity() <= 4096;
}
}
