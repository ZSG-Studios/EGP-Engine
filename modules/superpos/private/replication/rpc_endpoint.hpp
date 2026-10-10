// SPDX-License-Identifier: MIT
#pragma once
// Private C++23 typed RPC wire path for one Session (a replication link on the
// authority, or a spawner receiver on a client). Frames name a registered
// schema RPC by ID and target one replicated object handle; the wire can never
// select a method, script, resource or property. Authorization happens at
// execution: the authority runs World::authorize_rpc (permission, ownership
// revision, authority epoch, payload bound) when gameplay takes a call, and a
// client delivers an authority call only for a Ready replica whose schema
// declares the RPC.
//
// Every buffer is fixed and charged: an inbox of at most 64 frames / 64 KiB of
// payload, a pre-readiness buffer of at most 64 frames / 64 KiB per peer (plan
// L241), and at most 8 outstanding sends. A full inbox applies backpressure by
// leaving frames in the ordered Session channel; a full pre-readiness buffer
// drops the newest frame and counts it. Owner thread only; no user callbacks.
#include "private/charged_array.hpp"
#include <superpos/session.hpp>
#include <superpos/schema.hpp>
#include <array>

namespace superpos_egp::replication {

struct RpcFrame {
    superpos::RpcId rpc{};
    superpos::ObjectHandle handle{};
    std::uint64_t ownership_revision{};
    std::span<const std::byte> payload{};
};

// Wire v1: tag 0x52, version 1, varuint rpc, u64 handle, varuint ownership
// revision, varuint payload length, payload. Canonical varuints, no trailing
// bytes, nonzero rpc/handle/ownership revision, payload <= 4096 bytes.
inline constexpr std::size_t maximum_rpc_payload = 4096;
inline constexpr std::size_t maximum_rpc_frame = 2 + 10 + 8 + 10 + 10 + maximum_rpc_payload;
superpos::Result<std::size_t> encode_rpc(const RpcFrame &, std::span<std::byte>) noexcept;
superpos::Result<RpcFrame> decode_rpc(std::span<const std::byte>) noexcept;

enum class RpcDisposition : std::uint8_t { Deliver, Buffer, Drop };

struct RpcTotals {
    std::uint64_t sent{}, send_refused{}, retired{}, received{}, delivered{}, buffered{}, buffer_overflow{},
        expired{}, dropped{}, taken{}, rejected_permission{}, rejected_stale{}, rejected_unsupported{};
};

struct RpcEntry {
    bool used{};
    superpos::RpcId rpc{};
    superpos::ObjectHandle handle{};
    std::uint64_t ownership_revision{}, received_ms{};
    std::uint32_t offset{}, size{};
};

class RpcEndpoint {
public:
    static constexpr std::size_t maximum_entries = 64, maximum_bytes = 65536, maximum_outstanding = 8;
    static constexpr std::uint64_t buffer_lifetime_ms = 15000;
    using Entry = RpcEntry;
    // Bounded FIFO of frames with contiguous payload storage.
    class Queue {
    public:
        explicit Queue(superpos::Allocator &) noexcept;
        superpos::Status reserve() noexcept;
        bool push(const RpcFrame &, std::uint64_t now) noexcept;
        void remove(std::size_t index) noexcept;
        std::size_t size() const noexcept { return count_; }
        std::size_t bytes() const noexcept { return used_; }
        const Entry &at(std::size_t index) const noexcept { return entries_.span()[index]; }
        std::span<const std::byte> payload(std::size_t index) const noexcept;
        RpcFrame frame(std::size_t index) const noexcept;
    private:
        ChargedArray<Entry> entries_;
        ChargedArray<std::byte> arena_;
        std::size_t count_{}, used_{};
    };
    using Classifier = RpcDisposition (*)(void *context, const RpcFrame &) noexcept;

    explicit RpcEndpoint(superpos::Allocator &) noexcept;
    RpcEndpoint(const RpcEndpoint &) = delete;
    RpcEndpoint &operator=(const RpcEndpoint &) = delete;
    superpos::Status reserve(std::uint8_t channel) noexcept;
    std::uint8_t channel() const noexcept { return channel_; }
    bool reserved() const noexcept { return reserved_; }
    // Encode and send one frame on the reserved channel. Busy: eight sends are
    // still outstanding (retired once Applied); nothing was sent.
    superpos::Status send(superpos::Session &, superpos::Tick, const RpcFrame &) noexcept;
    // Retire Applied sends, read new frames and re-examine buffered ones.
    // classify decides per frame; Drop on a received frame is a protocol
    // violation, on a buffered frame an ordinary discard.
    superpos::Status pump(superpos::Session &, std::uint64_t now_ms, Classifier, void *context) noexcept;
    Queue &inbox() noexcept { return inbox_; }
    const Queue &inbox() const noexcept { return inbox_; }
    const Queue &pending() const noexcept { return pending_; }
    RpcTotals &totals() noexcept { return totals_; }
    const RpcTotals &totals() const noexcept { return totals_; }
    superpos::Error failure() const noexcept { return failure_; }
private:
    superpos::Status validate_channel(superpos::Session &) noexcept;
    superpos::Status fail_with(superpos::Error) noexcept;
    Queue inbox_, pending_;
    ChargedArray<std::byte> scratch_;
    std::array<superpos::DeliveryTicket, maximum_outstanding> outstanding_{};
    std::size_t outstanding_count_{};
    RpcTotals totals_{};
    std::uint8_t channel_{3};
    bool reserved_{}, validated_{};
    superpos::Error failure_{};
};
}
