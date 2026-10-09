// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/result.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

namespace superpos::service::admission { struct Grant; }
namespace superpos::service::control {
inline constexpr std::size_t request_header_bytes=96;
inline constexpr std::size_t maximum_body_bytes=4000;
inline constexpr std::size_t maximum_request_bytes=request_header_bytes+maximum_body_bytes;
inline constexpr std::size_t response_bytes=112;
inline constexpr std::uint64_t read_authority_permission=2,append_state_permission=4;
enum class Operation : std::uint8_t { ReadAuthority=1,AppendState=2 };
enum class ResponseStatus : std::uint8_t { Observed=1,Committed=2,Rejected=3,Unknown=4 };
// These version1 wire values are independent of the core Error enum ordinals.
enum class WireError : std::uint16_t {
    None=0,Protocol=1,Permission=2,Stale=3,NotReady=4,Busy=5,
    Timeout=6,Capacity=7,Counter=8,Storage=9,Unknown=10
};
struct Scope {
    std::uint64_t match{},session{},authority_epoch{},connection_incarnation{},actor{},principal_epoch{};
    bool operator==(const Scope&) const=default;
};
struct Request {
    Operation operation{Operation::ReadAuthority};
    std::uint64_t request_id{};
    Scope scope{};
    std::uint64_t append_id{},tick{};
    // Decode owns all metadata but borrows this exact canonical wire suffix.
    // The route copies it into its own bounded storage before callbacks/dispatch.
    std::span<const std::byte> body{};
};
struct Response {
    Operation operation{Operation::ReadAuthority};
    ResponseStatus status{ResponseStatus::Rejected};
    std::uint64_t request_id{};
    Scope scope{};
    std::uint64_t append_id{},tick{};
    WireError wire_error{WireError::NotReady};
    std::uint64_t journal_sequence{},retained_bytes{};
};
// Exact output length is required. Both encoders stage the complete bounded
// record before publication, permit aliases and leave output untouched on error.
Status encode_request(const Request&,std::span<std::byte>) noexcept;
Result<Request> decode_request(std::span<const std::byte>) noexcept;
Status encode_response(const Response&,std::span<std::byte>) noexcept;
Result<Response> decode_response(std::span<const std::byte>) noexcept;
// Compare the six encoded IDs exactly; require the operation bit in the seventh
// Grant field (permissions). principal_epoch is only admission.actor_epoch: it
// neither supplies a durable actor-operation epoch nor equates actor with PeerId.
Status matches_admission(const Request&,const admission::Grant&) noexcept;
WireError to_wire_error(Error) noexcept;
Result<Error> from_wire_error(WireError) noexcept;
}
