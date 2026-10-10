// SPDX-License-Identifier: MIT
#include "superpos/service/control_wire.hpp"
#include "superpos/service/admission_types.hpp"
#include <array>
#include <cstring>
#include <limits>
namespace superpos::service::control {
namespace {
bool range(const void* p,std::size_t n) noexcept {
    return !n||(p&&n<=UINTPTR_MAX-reinterpret_cast<std::uintptr_t>(p));
}
bool operation(Operation op) noexcept {return op==Operation::ReadAuthority||op==Operation::AppendState;}
bool scope(const Scope& s) noexcept {return s.match&&s.session&&s.connection_incarnation&&s.actor;}
bool metadata(Operation op,const Scope& s,std::uint64_t append,std::uint64_t tick) noexcept {
    return operation(op)&&scope(s)&&(op==Operation::ReadAuthority?(!append&&!tick):(s.authority_epoch&&append));
}
bool request(const Request& r) noexcept {
    return metadata(r.operation,r.scope,r.append_id,r.tick)&&r.body.size()<=maximum_body_bytes&&
        range(r.body.data(),r.body.size())&&(r.operation!=Operation::ReadAuthority||r.body.empty());
}
bool error(WireError e) noexcept {return static_cast<std::uint16_t>(e)<=10;}
bool response(const Response& r) noexcept {
    if(!metadata(r.operation,r.scope,r.append_id,r.tick)||!error(r.wire_error))return false;
    switch(r.status){
        case ResponseStatus::Observed:return r.operation==Operation::ReadAuthority&&r.wire_error==WireError::None;
        case ResponseStatus::Committed:return r.operation==Operation::AppendState&&r.wire_error==WireError::None;
        case ResponseStatus::Rejected:return r.wire_error!=WireError::None;
        case ResponseStatus::Unknown:return r.operation==Operation::AppendState&&r.wire_error==WireError::Unknown;
    }return false;
}
void put(std::span<std::byte> out,std::size_t offset,std::uint64_t value,unsigned n) noexcept {
    for(unsigned i=0;i<n;++i)out[offset+i]=std::byte((value>>(8*i))&255);
}
std::uint64_t get(std::span<const std::byte> in,std::size_t offset,unsigned n) noexcept {
    std::uint64_t value{};for(unsigned i=0;i<n;++i)value|=std::uint64_t(std::to_integer<unsigned>(in[offset+i]))<<(8*i);return value;
}
void common(std::span<std::byte> out,Operation op,std::uint64_t id,const Scope& s,std::uint64_t append,std::uint64_t tick) noexcept {
    out[4]=std::byte{1};out[6]=std::byte(static_cast<std::uint8_t>(op));put(out,16,id,8);
    put(out,24,s.match,8);put(out,32,s.session,8);put(out,40,s.authority_epoch,8);put(out,48,s.connection_incarnation,8);
    put(out,56,s.actor,8);put(out,64,s.principal_epoch,8);put(out,72,append,8);put(out,80,tick,8);
}
Scope read_scope(std::span<const std::byte> in) noexcept {return {get(in,24,8),get(in,32,8),get(in,40,8),get(in,48,8),get(in,56,8),get(in,64,8)};}
bool magic(std::span<const std::byte> in,char suffix) noexcept {
    return in[0]==std::byte{'S'}&&in[1]==std::byte{'P'}&&in[2]==std::byte{'O'}&&in[3]==std::byte(static_cast<unsigned char>(suffix));
}
bool zeros(std::span<const std::byte> in,std::size_t first,std::size_t last) noexcept {
    for(auto i=first;i<last;++i)if(in[i]!=std::byte{})return false;return true;
}
}
Status encode_request(const Request& value,std::span<std::byte> output) noexcept {
    if(value.body.size()>maximum_body_bytes)return fail(Error::CapacityExceeded);
    if(!request(value)||output.size()!=request_header_bytes+value.body.size()||!range(output.data(),output.size()))return fail(Error::InvalidArgument);
    std::array<std::byte,maximum_request_bytes> staged{};auto bytes=std::span(staged).first(output.size());
    bytes[0]=std::byte{'S'};bytes[1]=std::byte{'P'};bytes[2]=std::byte{'O'};bytes[3]=std::byte{'C'};bytes[5]=std::byte{1};
    put(bytes,8,bytes.size(),4);put(bytes,12,value.body.size(),4);common(bytes,value.operation,value.request_id,value.scope,value.append_id,value.tick);
    if(!value.body.empty())std::memcpy(bytes.data()+request_header_bytes,value.body.data(),value.body.size());
    std::memcpy(output.data(),bytes.data(),bytes.size());return {};
}
Result<Request> decode_request(std::span<const std::byte> input) noexcept {
    if(input.size()<request_header_bytes)return fail(Error::Truncated);
    if(input.size()>maximum_request_bytes)return fail(Error::CapacityExceeded);
    if(!range(input.data(),input.size()))return fail(Error::InvalidArgument);
    if(!magic(input,'C')||input[4]!=std::byte{1}||input[5]!=std::byte{1}||input[7]!=std::byte{}||
        get(input,8,4)!=input.size()||get(input,12,4)!=input.size()-request_header_bytes||!zeros(input,88,96))return fail(Error::ProtocolViolation);
    Request result{static_cast<Operation>(std::to_integer<std::uint8_t>(input[6])),get(input,16,8),read_scope(input),get(input,72,8),get(input,80,8),input.subspan(request_header_bytes)};
    if(!request(result))return fail(Error::ProtocolViolation);return result;
}
Status encode_response(const Response& value,std::span<std::byte> output) noexcept {
    if(!response(value)||output.size()!=response_bytes||!range(output.data(),output.size()))return fail(Error::InvalidArgument);
    std::array<std::byte,response_bytes> bytes{};bytes[0]=std::byte{'S'};bytes[1]=std::byte{'P'};bytes[2]=std::byte{'O'};bytes[3]=std::byte{'R'};bytes[5]=std::byte{2};bytes[7]=std::byte(static_cast<std::uint8_t>(value.status));
    put(bytes,8,response_bytes,4);common(bytes,value.operation,value.request_id,value.scope,value.append_id,value.tick);
    put(bytes,88,static_cast<std::uint16_t>(value.wire_error),2);put(bytes,96,value.journal_sequence,8);put(bytes,104,value.retained_bytes,8);
    std::memcpy(output.data(),bytes.data(),bytes.size());return {};
}
Result<Response> decode_response(std::span<const std::byte> input) noexcept {
    if(input.size()<response_bytes)return fail(Error::Truncated);
    if(input.size()!=response_bytes||!range(input.data(),input.size()))return fail(Error::InvalidArgument);
    if(!magic(input,'R')||input[4]!=std::byte{1}||input[5]!=std::byte{2}||get(input,8,4)!=response_bytes||get(input,12,4)||!zeros(input,90,96))return fail(Error::ProtocolViolation);
    Response result{static_cast<Operation>(std::to_integer<std::uint8_t>(input[6])),static_cast<ResponseStatus>(std::to_integer<std::uint8_t>(input[7])),get(input,16,8),read_scope(input),get(input,72,8),get(input,80,8),static_cast<WireError>(get(input,88,2)),get(input,96,8),get(input,104,8)};
    if(!response(result))return fail(Error::ProtocolViolation);return result;
}
Status matches_admission(const Request& value,const admission::Grant& grant) noexcept {
    if(!request(value))return fail(Error::InvalidArgument);
    const Scope admitted{grant.match,grant.session,grant.authority_epoch,grant.connection_incarnation,grant.actor,grant.actor_epoch};
    const auto required=value.operation==Operation::ReadAuthority?read_authority_permission:append_state_permission;
    if(value.scope!=admitted||!grant.permissions||(grant.permissions&~admission::known_permissions)||(grant.permissions&required)!=required)return fail(Error::PermissionDenied);return {};
}
WireError to_wire_error(Error value) noexcept {
    switch(value){
        case Error::None:return WireError::None;
        case Error::PermissionDenied:case Error::AuthenticationFailed:return WireError::Permission;
        case Error::StaleEpoch:case Error::StaleGeneration:case Error::MissingBaseline:return WireError::Stale;
        case Error::NotReady:case Error::RecoveryUnavailable:return WireError::NotReady;
        case Error::Busy:return WireError::Busy;
        case Error::Timeout:return WireError::Timeout;
        case Error::CapacityExceeded:case Error::OutOfMemory:return WireError::Capacity;
        case Error::CounterExhausted:case Error::Overflow:return WireError::Counter;
        case Error::Io:case Error::ChannelFailed:return WireError::Storage;
        case Error::UnknownOutcome:return WireError::Unknown;
        case Error::InvalidArgument:case Error::ProtocolViolation:case Error::NonCanonical:case Error::Truncated:case Error::Unsupported:case Error::IncompatibleSchema:return WireError::Protocol;
    }return WireError::Protocol;
}
Result<Error> from_wire_error(WireError value) noexcept {
    switch(value){
        case WireError::None:return Error::None;
        case WireError::Protocol:return Error::ProtocolViolation;
        case WireError::Permission:return Error::PermissionDenied;
        case WireError::Stale:return Error::StaleEpoch;
        case WireError::NotReady:return Error::NotReady;
        case WireError::Busy:return Error::Busy;
        case WireError::Timeout:return Error::Timeout;
        case WireError::Capacity:return Error::CapacityExceeded;
        case WireError::Counter:return Error::CounterExhausted;
        case WireError::Storage:return Error::Io;
        case WireError::Unknown:return Error::UnknownOutcome;
    }return fail(Error::ProtocolViolation);
}
}
