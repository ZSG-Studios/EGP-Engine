// Copyright (c) 2026 Superpos contributors.
// SPDX-License-Identifier: MIT
// Superpos maintained private backend record boundary. Complete oversize records
// are rejected, never delivered as a valid prefix. This is not an SCTP parser.
#pragma once
#include <array>
#include <span>
#include <cstddef>
#include <cstdint>
#include <algorithm>
namespace rtc::impl {
enum class RecordAdmission : std::uint8_t { Incomplete,Complete,Rejected };
struct RecordTag {std::uint16_t stream{};std::uint32_t ppid{};bool operator==(const RecordTag&)const noexcept=default;};
template<std::size_t Capacity> class SuperposRecord {
public:
    RecordAdmission append(std::span<const std::byte> chunk,bool end,RecordTag tag={}) noexcept {
        complete_=false;
        if(!active_){size_=0;discard_=false;tag_=tag;active_=true;}
        if(tag!=tag_||chunk.size()>Capacity-size_)discard_=true;
        if(!discard_){std::copy(chunk.begin(),chunk.end(),bytes_.begin()+size_);size_+=chunk.size();}
        if(!end)return discard_?RecordAdmission::Rejected:RecordAdmission::Incomplete;
        active_=false;
        if(discard_){size_=0;return RecordAdmission::Rejected;}
        complete_=true;return RecordAdmission::Complete;
    }
    // Only Complete permits publication of this span. Until the next append,
    // the complete record borrows these fixed bytes. No callback sees prefixes.
    std::span<const std::byte> bytes()const noexcept{return std::span(bytes_).first(complete_?size_:0);}
    void reject(bool end) noexcept {size_=0;complete_=false;discard_=true;active_=!end;}
    void clear() noexcept {size_=0;active_=discard_=complete_=false;}
private:
    std::array<std::byte,Capacity> bytes_{};std::size_t size_{};
    RecordTag tag_{};bool active_{},discard_{},complete_{};
};
}
