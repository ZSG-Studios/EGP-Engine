#pragma once
#include "allocator.hpp"
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
inline constexpr std::uint8_t lockstep_command_wire_version = 2;

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
    // Explicitly drops the oldest unacknowledged inputs, for an owner whose
    // history filled during an outage and must keep recording; the server's
    // playout skips them as lost. Returns how many were dropped.
    std::size_t discard_oldest(std::size_t count) noexcept;
    // The newest unacknowledged inputs, up to max_ticks and what fits (older ones
    // the server never received are skipped by its playout); command_acknowledged
    // reports the newest relayed command tick this client holds in order.
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

// Command stream sizing. History is allocated once at creation from the owner's
// allocator (MemoryDomain::History) and never grows. The encoder's history is the
// window in which a client can fall behind (a slow keyframe join, a stall) and still
// catch up without another keyframe; the decoder's is how far ahead of its own
// simulation a client may buffer.
struct CommandStreamConfig {
    std::size_t input_bytes{}, slots{};
    std::size_t history_ticks{lockstep_history_ticks};
    // Changed-slot entries shared by all retained ticks; 0 sizes for every slot
    // changing on every retained tick.
    std::size_t change_capacity{};
    // Decoder only: out-of-order batches parked beyond a gap (selective repeat), so
    // one lost datagram costs one repair, not a resend of everything after it.
    std::size_t pending_batches{64}, pending_batch_bytes{1200};
};
inline constexpr std::size_t lockstep_max_pending_batches = 64;
inline constexpr std::size_t lockstep_max_command_history = std::size_t{1} << 16;
inline constexpr std::uint32_t lockstep_no_recipient = 0xFFFFFFFFu;

// Server: per-tick command tables (one input per slot) with change-only history.
// Wire v2 sends each changed slot as a slot gap, a changed-byte mask and only the
// changed bytes, so a slowly varying input (a turning yaw) costs a few bytes, not
// its full width. A recipient's own processed client ticks can ride along so a
// predicting client reconciles without stamping ticks into the broadcast inputs.
class CommandEncoder {
public:
    static Result<CommandEncoder> create(Allocator&, CommandStreamConfig) noexcept;
    // Ticks are contiguous. A slot not set during a tick keeps its previous input
    // and processed tick.
    Status begin(Tick) noexcept;
    Status set(std::uint16_t slot, std::span<const std::byte> input, Tick processed = 0) noexcept;
    Status end() noexcept;
    // Every sealed tick after `after`, oldest first, until output is full. Fails with
    // StaleEpoch when `after` predates retained history (client needs a keyframe).
    Result<std::size_t> encode(Tick after, std::span<std::byte> output, std::uint32_t recipient = lockstep_no_recipient) const noexcept;
    // Fresh-first streaming window for unreliable carriers. Every tick after `sent`
    // that fits goes out (oldest first); remaining space then carries up to
    // `redundant` already-sent ticks after `acknowledged`, so an isolated lost
    // batch heals without a round trip while fresh ticks never starve. With nothing
    // fresh it resends the newest unacknowledged ticks. StaleEpoch as for encode.
    Result<std::size_t> encode_window(Tick acknowledged, Tick sent, std::span<std::byte> output,
        std::uint32_t recipient = lockstep_no_recipient, std::size_t redundant = 0) const noexcept;
    // Complete table at the newest sealed tick, for join keyframes.
    Result<std::size_t> encode_table(std::span<std::byte> output) const noexcept;
    Tick newest() const noexcept { return newest_; }
    Tick oldest() const noexcept { return ticks_ ? newest_-ticks_+1 : newest_+1; }
    std::size_t retained() const noexcept { return ticks_; }
private:
    struct Change { std::uint16_t slot{}, mask{}; std::array<std::byte, lockstep_input_bytes> bytes{}; };
    struct TickMeta { std::size_t first{}, count{}, bytes{}; };
    CommandEncoder(Allocator& allocator, CommandStreamConfig config) noexcept
        : config_(config), changes_(allocator, MemoryDomain::History), meta_(allocator, MemoryDomain::History), processed_(allocator, MemoryDomain::History) {}
    Change* changes() const noexcept { return reinterpret_cast<Change*>(const_cast<std::byte*>(changes_.bytes().data())); }
    TickMeta* meta() const noexcept { return reinterpret_cast<TickMeta*>(const_cast<std::byte*>(meta_.bytes().data())); }
    Tick* processed() const noexcept { return reinterpret_cast<Tick*>(const_cast<std::byte*>(processed_.bytes().data())); }
    // Whole ticks from `first` (after `last`, already holding `body` bytes) that fit.
    std::size_t fit_forward(Tick first, Tick last, std::size_t body, std::size_t capacity, std::uint32_t recipient) const noexcept;
    Result<std::size_t> write(Tick first, std::size_t ticks, std::span<std::byte> output, std::uint32_t recipient) const noexcept;
    CommandStreamConfig config_{};
    Buffer changes_, meta_, processed_;
    std::array<std::array<std::byte, lockstep_input_bytes>, lockstep_slots> table_{}, staged_{};
    std::array<Tick, lockstep_slots> staged_processed_{};
    std::size_t change_head_{}, change_count_{}, ticks_{};
    Tick newest_{}, open_{};
    bool sealed_any_{}, open_any_{};
};

// Per-recipient sender over an unreliable channel: fresh-first windows, one
// rewind to the acknowledgement after a stall of 2 x sRTT plus the client's ack
// cadence, and redundancy only while the recipient is caught up (a client that
// is behind needs goodput, not copies). The caller supplies the recipient's
// newest in-order acknowledgement and the carrier's smoothed RTT.
struct CommandStreamPolicy {
    std::size_t redundant_ticks{1};
    // Behind by more than this many ticks: no redundancy.
    Tick catch_up_ticks{30};
    std::uint64_t minimum_stall_us{100000}, ack_cadence_us{70000};
};
struct CommandStreamStatus { Tick acknowledged{}, sent{}; std::uint64_t batches{}, rewinds{}; };
class CommandStream {
public:
    explicit CommandStream(CommandStreamPolicy policy = {}) noexcept : policy_(policy) {}
    // Keyframe join (or rejoin) at tick: the stream resumes after it.
    void reset(Tick keyframe, std::uint64_t now_us) noexcept;
    // Next batch for this recipient. StaleEpoch means its acknowledgement left
    // the encoder's history: send a keyframe and reset.
    Result<std::size_t> next(const CommandEncoder&, Tick acknowledged, std::uint64_t now_us, std::uint64_t srtt_us,
        std::span<std::byte> output, std::uint32_t recipient = lockstep_no_recipient) noexcept;
    CommandStreamStatus status() const noexcept { return {acknowledged_, sent_, batches_, rewinds_}; }
private:
    CommandStreamPolicy policy_{};
    Tick acknowledged_{}, sent_{};
    std::uint64_t acknowledged_at_{}, batches_{}, rewinds_{};
    bool started_{};
};

// Client: applies relayed ticks strictly in order onto a full command table.
class CommandDecoder {
public:
    static Result<CommandDecoder> create(Allocator&, CommandStreamConfig) noexcept;
    // Keyframe: complete table at tick; subsequent batches continue from it.
    Status load_table(std::span<const std::byte>) noexcept;
    // Older ticks are ignored. A batch beyond a gap is validated and parked, then
    // applied once the gap fills (when parking is full the farthest batch yields).
    // A batch beyond the buffer is rejected (CapacityExceeded) and resent later.
    Status accept(std::span<const std::byte>) noexcept;
    // Parked out-of-order batches awaiting a gap repair.
    std::size_t deferred() const noexcept;
    // Applies the next buffered tick to the table and returns it; Busy when none.
    Result<Tick> advance() noexcept;
    std::span<const std::byte> input(std::uint16_t slot) const noexcept;
    // The recipient's own client tick consumed at the applied tick (0 when unknown).
    Tick processed() const noexcept { return processed_tick_; }
    Tick applied() const noexcept { return applied_; }
    Tick newest() const noexcept { return newest_; }
    std::size_t buffered() const noexcept { return ticks_; }
private:
    struct Change { std::uint16_t slot{}; std::array<std::byte, lockstep_input_bytes> bytes{}; };
    struct TickMeta { std::size_t first{}, count{}; Tick processed{}; };
    struct Parked { bool occupied{}; Tick first{}, last{}; std::size_t size{}; };
    CommandDecoder(Allocator& allocator, CommandStreamConfig config) noexcept
        : config_(config), changes_(allocator, MemoryDomain::History), meta_(allocator, MemoryDomain::History), parked_bytes_(allocator, MemoryDomain::History) {}
    Status accept_contiguous(std::span<const std::byte>) noexcept;
    bool park(std::span<const std::byte>) noexcept;
    void drain() noexcept;
    Change* changes() const noexcept { return reinterpret_cast<Change*>(const_cast<std::byte*>(changes_.bytes().data())); }
    TickMeta* meta() const noexcept { return reinterpret_cast<TickMeta*>(const_cast<std::byte*>(meta_.bytes().data())); }
    CommandStreamConfig config_{};
    Buffer changes_, meta_, parked_bytes_;
    std::array<Parked, lockstep_max_pending_batches> parked_{};
    // table_ is the state at applied_, latest_ the state at newest_ (mask base).
    std::array<std::array<std::byte, lockstep_input_bytes>, lockstep_slots> table_{}, latest_{};
    std::size_t change_head_{}, change_count_{}, tick_head_{}, ticks_{};
    Tick applied_{}, newest_{}, processed_tick_{};
    bool synchronized_{};
};
}
