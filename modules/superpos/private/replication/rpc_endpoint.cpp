// SPDX-License-Identifier: MIT
#include "rpc_endpoint.hpp"
#include <superpos/codec.hpp>
#include <algorithm>
#include <cstring>

namespace superpos_egp::replication {
using superpos::Error;
using superpos::fail;
using superpos::MemoryDomain;

namespace {
constexpr std::byte rpc_tag{0x52};
constexpr std::byte rpc_version{0x01};
}

superpos::Result<std::size_t> encode_rpc(const RpcFrame &frame, std::span<std::byte> output) noexcept {
    if (!frame.rpc || !frame.handle || !frame.ownership_revision || frame.payload.size() > maximum_rpc_payload) return fail(Error::InvalidArgument);
    superpos::Writer writer(output);
    const std::array<std::byte, 2> header{rpc_tag, rpc_version};
    if (!writer.raw(header) || !writer.varuint(frame.rpc) || !writer.u64(frame.handle.value) ||
            !writer.varuint(frame.ownership_revision) || !writer.varuint(frame.payload.size()) || !writer.raw(frame.payload))
        return fail(Error::CapacityExceeded);
    return writer.size();
}

superpos::Result<RpcFrame> decode_rpc(std::span<const std::byte> input) noexcept {
    superpos::Reader reader(input);
    auto header = reader.raw(2);
    if (!header) return fail(Error::Truncated);
    if ((*header)[0] != rpc_tag || (*header)[1] != rpc_version) return fail(Error::ProtocolViolation);
    auto rpc = reader.varuint();
    if (!rpc) return fail(rpc.error());
    auto handle = reader.u64();
    if (!handle) return fail(handle.error());
    auto ownership = reader.varuint();
    if (!ownership) return fail(ownership.error());
    auto size = reader.varuint();
    if (!size) return fail(size.error());
    if (*size > maximum_rpc_payload) return fail(Error::CapacityExceeded);
    auto payload = reader.raw(std::size_t(*size));
    if (!payload) return fail(payload.error());
    if (!reader.empty()) return fail(Error::ProtocolViolation);
    RpcFrame frame{*rpc, superpos::ObjectHandle{*handle}, *ownership, *payload};
    if (!frame.rpc || !frame.handle || !frame.ownership_revision) return fail(Error::ProtocolViolation);
    return frame;
}

RpcEndpoint::Queue::Queue(superpos::Allocator &allocator) noexcept
    : entries_(allocator, MemoryDomain::Session), arena_(allocator, MemoryDomain::Session) {}

superpos::Status RpcEndpoint::Queue::reserve() noexcept {
    if (auto status = entries_.initialize(maximum_entries); !status) return status;
    return arena_.initialize(maximum_bytes);
}

bool RpcEndpoint::Queue::push(const RpcFrame &frame, std::uint64_t now) noexcept {
    if (count_ == maximum_entries || frame.payload.size() > maximum_bytes - used_) return false;
    auto &entry = entries_[count_];
    entry = {true, frame.rpc, frame.handle, frame.ownership_revision, now, std::uint32_t(used_), std::uint32_t(frame.payload.size())};
    if (!frame.payload.empty()) std::memcpy(arena_.span().data() + used_, frame.payload.data(), frame.payload.size());
    used_ += frame.payload.size();
    ++count_;
    return true;
}

void RpcEndpoint::Queue::remove(std::size_t index) noexcept {
    if (index >= count_) return;
    const auto removed = entries_[index];
    // Payloads stay contiguous in FIFO order: close the gap.
    const std::size_t tail = used_ - (removed.offset + removed.size);
    if (tail) std::memmove(arena_.span().data() + removed.offset, arena_.span().data() + removed.offset + removed.size, tail);
    used_ -= removed.size;
    for (std::size_t i = index + 1; i < count_; ++i) {
        entries_[i - 1] = entries_[i];
        entries_[i - 1].offset -= removed.size;
    }
    entries_[--count_] = Entry{};
}

std::span<const std::byte> RpcEndpoint::Queue::payload(std::size_t index) const noexcept {
    const auto &entry = entries_.span()[index];
    return arena_.span().subspan(entry.offset, entry.size);
}

RpcFrame RpcEndpoint::Queue::frame(std::size_t index) const noexcept {
    const auto &entry = entries_.span()[index];
    return {entry.rpc, entry.handle, entry.ownership_revision, payload(index)};
}

RpcEndpoint::RpcEndpoint(superpos::Allocator &allocator) noexcept
    : inbox_(allocator), pending_(allocator), scratch_(allocator, MemoryDomain::Session) {}

superpos::Status RpcEndpoint::fail_with(Error error) noexcept {
    if (failure_ == Error::None) failure_ = error;
    return fail(error);
}

superpos::Status RpcEndpoint::reserve(std::uint8_t channel) noexcept {
    if (reserved_) return fail(Error::Busy);
    if (channel >= 32) return fail(Error::InvalidArgument);
    for (auto status : {inbox_.reserve(), pending_.reserve(), scratch_.initialize(maximum_rpc_frame)})
        if (!status) return status;
    channel_ = channel;
    reserved_ = true;
    return {};
}

superpos::Status RpcEndpoint::validate_channel(superpos::Session &session) noexcept {
    if (validated_) return {};
    auto mode = session.channel_mode(channel_);
    if (!mode) return fail(mode.error());
    auto purpose = session.channel_purpose(channel_);
    if (!purpose) return fail(purpose.error());
    // Calls are ordered per peer and never superseded or lost.
    if (*mode != superpos::DeliveryMode::ReliableOrdered || *purpose != superpos::ChannelPurpose::Application) return fail_with(Error::Unsupported);
    validated_ = true;
    return {};
}

superpos::Status RpcEndpoint::send(superpos::Session &session, superpos::Tick tick, const RpcFrame &frame) noexcept {
    if (failure_ != Error::None) return fail(failure_);
    if (!reserved_) return fail(Error::NotReady);
    if (!session.ready()) return fail(Error::NotReady);
    if (auto checked = validate_channel(session); !checked) return checked;
    if (outstanding_count_ == maximum_outstanding) { ++totals_.send_refused; return fail(Error::Busy); }
    auto encoded = encode_rpc(frame, scratch_.span());
    if (!encoded) return fail(encoded.error());
    auto ticket = session.send(scratch_.span().first(*encoded), tick, channel_);
    if (!ticket) {
        if (ticket.error() == Error::Busy || ticket.error() == Error::CapacityExceeded) { ++totals_.send_refused; return fail(Error::Busy); }
        return fail_with(ticket.error());
    }
    outstanding_[outstanding_count_++] = *ticket;
    ++totals_.sent;
    return {};
}

superpos::Status RpcEndpoint::pump(superpos::Session &session, std::uint64_t now, Classifier classify, void *context) noexcept {
    if (failure_ != Error::None) return fail(failure_);
    if (!reserved_ || !session.ready()) return {};
    if (auto checked = validate_channel(session); !checked) return checked;
    // Retire sends whose delivery reached Applied; keep order compact.
    for (std::size_t i = 0; i < outstanding_count_;) {
        auto outcome = session.outcome(outstanding_[i]);
        if (!outcome) return fail_with(outcome.error());
        if (*outcome != superpos::Outcome::Applied) { ++i; continue; }
        if (auto retired = session.retire(outstanding_[i]); !retired) return fail_with(retired.error());
        ++totals_.retired;
        outstanding_[i] = outstanding_[--outstanding_count_];
    }
    // Buffered frames first, so a replica that became ready receives its calls
    // in arrival order before any newer one.
    for (std::size_t i = 0; i < pending_.size();) {
        const auto frame = pending_.frame(i);
        const auto disposition = now - pending_.at(i).received_ms > buffer_lifetime_ms ? RpcDisposition::Drop : classify(context, frame);
        if (disposition == RpcDisposition::Buffer) { ++i; continue; }
        if (disposition == RpcDisposition::Deliver) {
            if (!inbox_.push(frame, now)) break; // inbox full: retry next pump
            ++totals_.delivered;
        } else {
            if (now - pending_.at(i).received_ms > buffer_lifetime_ms) ++totals_.expired; else ++totals_.dropped;
        }
        pending_.remove(i);
    }
    for (unsigned n = 0; n < maximum_entries; ++n) {
        auto delivery = session.receive(channel_);
        if (!delivery) {
            if (delivery.error() == Error::NotReady || delivery.error() == Error::Busy) break;
            return fail_with(delivery.error());
        }
        auto frame = decode_rpc(delivery->payload);
        if (!frame) return fail_with(Error::ProtocolViolation);
        const auto disposition = classify(context, *frame);
        if (disposition == RpcDisposition::Drop) return fail_with(Error::ProtocolViolation);
        if (disposition == RpcDisposition::Deliver) {
            // A full inbox leaves this frame unapplied in the ordered channel.
            if (pending_.size() || !inbox_.push(*frame, now)) {
                if (!pending_.size()) break;
                if (!pending_.push(*frame, now)) break;
                ++totals_.buffered;
            } else {
                ++totals_.delivered;
            }
        } else if (!pending_.push(*frame, now)) {
            ++totals_.buffer_overflow; // newest frame dropped, counted
        } else {
            ++totals_.buffered;
        }
        ++totals_.received;
        if (auto applied = session.applied(delivery->message, channel_); !applied) return fail_with(applied.error());
    }
    return {};
}
}
