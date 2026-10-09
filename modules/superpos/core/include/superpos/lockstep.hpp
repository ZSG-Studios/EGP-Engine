#pragma once
#include "types.hpp"
#include <array>
#include <cstdint>
#include <span>

namespace superpos {
// Deterministic-simulation input streaming. Clients record exactly one input per
// simulation tick and resend every input the server has not yet reported received,
// so an unreliable channel never loses a tick. The server plays each peer's inputs
// out through an adaptive jitter buffer, one per server tick, and relays the
// consumed inputs of all peers as a delta-compressed command stream that lets every
// client advance the same deterministic world. All storage is fixed and bounded;
// every method belongs to the owning thread.
inline constexpr std::size_t lockstep_input_bytes = 16;
inline constexpr std::size_t lockstep_history_ticks = 128;
inline constexpr std::size_t lockstep_slots = 256;
inline constexpr std::uint8_t lockstep_wire_version = 1;

struct InputBatchHeader { Tick command_acknowledged{}; Tick first{}; std::size_t count{}, input_bytes{}; };

// Client side: tick-stamped inputs retained until acknowledged by the server.
class InputHistory {
public:
    static Result<InputHistory> create(std::size_t input_bytes) noexcept;
    // Ticks must be contiguous; capacity exhaustion means the server stopped
    // acknowledging and the owner must resynchronize, never silently drop input.
    Status record(Tick tick, std::span<const std::byte> input) noexcept;
    // Server reported every input up to and including tick as received.
    Status acknowledge(Tick received) noexcept;
    // Oldest unacknowledged inputs first; command_acknowledged reports the newest
    // relayed command tick this client applied (drives server delta range).
    Result<std::size_t> encode(Tick command_acknowledged, std::size_t max_ticks, std::span<std::byte> output) const noexcept;
    std::size_t pending() const noexcept { return count_; }
    Tick newest() const noexcept { return newest_; }
private:
    explicit InputHistory(std::size_t input_bytes) noexcept : input_bytes_(input_bytes) {}
    std::array<std::array<std::byte, lockstep_input_bytes>, lockstep_history_ticks> inputs_{};
    std::size_t input_bytes_{}, count_{};
    Tick oldest_{}, newest_{};
    bool any_{};
};

struct PlayoutConfig {
    std::size_t input_bytes{};
    std::size_t initial_target{6}, minimum_target{2}, maximum_target{45};
    // Buffer target shrinks by one after this many ticks without starvation.
    Tick relax_ticks{240};
    // Consume two inputs in a tick while the buffer exceeds target+margin.
    std::size_t catch_up_margin{12};
};
struct PlayoutStatus {
    Tick received{}, consumed{}, command_acknowledged{};
    std::size_t buffered{}, target{};
    std::uint64_t starvations{}, duplicates{}, late{}, skipped{};
    bool playing{};
};
struct PlayoutInput { Tick tick{}; std::span<const std::byte> bytes{}; };

// Server side, one per peer: adaptive jitter buffer over redundant batches.
class InputPlayout {
public:
    static Result<InputPlayout> create(PlayoutConfig) noexcept;
    Status accept(std::span<const std::byte> batch) noexcept;
    // One server tick: 0 inputs while buffering/starved, 1 normally, 2 when catching up.
    Result<std::size_t> consume(std::span<PlayoutInput, 2> output) noexcept;
    PlayoutStatus status() const noexcept;
    // Client clock advice in [-1,1]: positive asks the client to simulate slightly
    // faster (buffer below target), negative slower (buffer above target).
    double pace_advice() const noexcept;
private:
    explicit InputPlayout(PlayoutConfig config) noexcept : config_(config), target_(config.initial_target) {}
    PlayoutConfig config_{};
    std::array<std::array<std::byte, lockstep_input_bytes>, lockstep_history_ticks> inputs_{};
    std::array<bool, lockstep_history_ticks> occupied_{};
    std::array<std::array<std::byte, lockstep_input_bytes>, 2> consumed_{};
    Tick next_{}, received_{}, command_acknowledged_{}, calm_{};
    std::size_t target_{};
    std::uint64_t starvations_{}, duplicates_{}, late_{}, skipped_{};
    bool started_{}, playing_{};
};

// Server: per-tick command tables (one input per slot) with change-only history.
class CommandEncoder {
public:
    static Result<CommandEncoder> create(std::size_t input_bytes, std::size_t slots) noexcept;
    // Ticks are contiguous. A slot not set during a tick keeps its previous input.
    Status begin(Tick) noexcept;
    Status set(std::uint16_t slot, std::span<const std::byte> input) noexcept;
    Status end() noexcept;
    // Every sealed tick after `after`, oldest first, until output is full. Fails with
    // StaleEpoch when `after` predates retained history (client needs a keyframe).
    Result<std::size_t> encode(Tick after, std::span<std::byte> output) const noexcept;
    // Complete table at the newest sealed tick, for join keyframes.
    Result<std::size_t> encode_table(std::span<std::byte> output) const noexcept;
    Tick newest() const noexcept { return newest_; }
private:
    CommandEncoder(std::size_t input_bytes, std::size_t slots) noexcept : input_bytes_(input_bytes), slots_(slots) {}
    struct Change { std::uint16_t slot{}; std::array<std::byte, lockstep_input_bytes> bytes{}; };
    static constexpr std::size_t change_capacity = 4096;
    std::array<std::array<std::byte, lockstep_input_bytes>, lockstep_slots> table_{}, staged_{};
    std::array<Change, change_capacity> changes_{};
    std::array<std::size_t, lockstep_history_ticks> tick_first_{}, tick_count_{};
    std::size_t input_bytes_{}, slots_{}, change_head_{}, change_count_{}, ticks_{};
    Tick newest_{}, open_{};
    bool sealed_any_{}, open_any_{};
};

// Client: applies relayed ticks strictly in order onto a full command table.
class CommandDecoder {
public:
    static Result<CommandDecoder> create(std::size_t input_bytes, std::size_t slots) noexcept;
    // Keyframe: complete table at tick; subsequent batches continue from it.
    Status load_table(std::span<const std::byte>) noexcept;
    // Older ticks are ignored, a gap after newest is rejected (Unsupported).
    Status accept(std::span<const std::byte>) noexcept;
    // Applies the next buffered tick to the table and returns it; Busy when none.
    Result<Tick> advance() noexcept;
    std::span<const std::byte> input(std::uint16_t slot) const noexcept;
    Tick applied() const noexcept { return applied_; }
    Tick newest() const noexcept { return newest_; }
    std::size_t buffered() const noexcept { return ticks_; }
private:
    CommandDecoder(std::size_t input_bytes, std::size_t slots) noexcept : input_bytes_(input_bytes), slots_(slots) {}
    struct Change { std::uint16_t slot{}; std::array<std::byte, lockstep_input_bytes> bytes{}; };
    static constexpr std::size_t change_capacity = 4096;
    std::array<std::array<std::byte, lockstep_input_bytes>, lockstep_slots> table_{};
    std::array<Change, change_capacity> changes_{};
    std::array<std::size_t, lockstep_history_ticks> tick_first_{}, tick_count_{};
    std::size_t input_bytes_{}, slots_{}, change_head_{}, change_count_{}, tick_head_{}, ticks_{};
    Tick applied_{}, newest_{};
    bool synchronized_{};
};
}
