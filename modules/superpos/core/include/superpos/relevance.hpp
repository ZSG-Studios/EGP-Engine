#pragma once
#include "types.hpp"
#include <optional>
#include <span>
namespace superpos {
struct RegionEntry {
    bool occupied{};
    ObjectHandle handle{};
    std::uint32_t region{};
    std::optional<std::uint32_t> previous{},next{};
};
struct RegionBucket { std::optional<std::uint32_t> first{}; std::uint32_t count{}; };
// Owner-thread intrusive region index. A query touches its region's objects,
// never the complete world. Slot indices remain internal; exposed IDs retain
// their full generation. Empty links use explicit presence, not integer sentinels.
class RegionIndex {
    std::span<RegionEntry> entries_{};
    std::span<RegionBucket> regions_{};
    std::uint32_t maximum_per_region_{};
    RegionIndex(std::span<RegionEntry>,std::span<RegionBucket>,std::uint32_t) noexcept;
    Result<std::uint32_t> find(ObjectHandle) const noexcept;
    void unlink(std::uint32_t) noexcept;
    void link(std::uint32_t,std::uint32_t) noexcept;
public:
    static Result<RegionIndex> create(std::span<RegionEntry>,std::span<RegionBucket>,std::uint32_t maximum_per_region=1000) noexcept;
    RegionIndex(const RegionIndex&)=delete;
    RegionIndex& operator=(const RegionIndex&)=delete;
    RegionIndex(RegionIndex&&) noexcept=default;
    RegionIndex& operator=(RegionIndex&&) noexcept=default;
    Status insert(ObjectHandle,std::uint32_t region) noexcept;
    Status move(ObjectHandle,std::uint32_t region) noexcept;
    Status remove(ObjectHandle) noexcept;
    Result<std::size_t> collect(std::uint32_t region,std::span<ObjectHandle>) const noexcept;
    Result<std::uint32_t> region(ObjectHandle) const noexcept;
    Result<std::size_t> count(std::uint32_t region) const noexcept;
};
enum class Visibility : std::uint8_t { Hidden,Visible,Dormant };
struct ReplicationDecision { Visibility visibility{Visibility::Visible}; std::uint16_t weight{1}; };
class ReplicationPolicy {
public:
    virtual ~ReplicationPolicy()=default;
    virtual ReplicationDecision decide(PeerId,ObjectHandle,Tick) const noexcept=0;
};
}
