#pragma once
#include "superpos/sqlite_journal.hpp"
#include <array>
namespace superpos {
// These views borrow immutable envelope bytes. Keep those bytes alive and unchanged
// until all consumers finish. Parsing performs no effects or gameplay callbacks.
struct JournalEnvelopeView {
    std::array<DurableDecision,64> decisions{};
    std::array<DurableEffect,64> effects{};
    std::uint16_t decision_count{},effect_count{};
};
// Exact decoder for the v1 SQLite lossless append envelope. Empty input is the
// persisted plain-append marker. Validate schema/state and contiguous journal
// coverage separately; a decoded envelope never authorizes business execution.
Result<JournalEnvelopeView> decode_journal_envelope(std::uint64_t expected_match,
    std::span<const std::byte> encoded) noexcept;
}
