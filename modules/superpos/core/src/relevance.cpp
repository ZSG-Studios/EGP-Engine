#include "superpos/relevance.hpp"
#include <limits>
namespace superpos {
namespace {
bool overlap(std::span<const std::byte> first,std::span<const std::byte> second) noexcept {
    const auto a=reinterpret_cast<std::uintptr_t>(first.data()),b=reinterpret_cast<std::uintptr_t>(second.data());
    if(first.size()>std::numeric_limits<std::uintptr_t>::max()-a || second.size()>std::numeric_limits<std::uintptr_t>::max()-b)return true;
    return !first.empty()&&!second.empty()&&a<b+second.size()&&b<a+first.size();
}
}
RegionIndex::RegionIndex(std::span<RegionEntry> entries,std::span<RegionBucket> regions,std::uint32_t maximum) noexcept:
    entries_(entries),regions_(regions),maximum_per_region_(maximum) {
    for(auto& entry:entries_)entry={};for(auto& region:regions_)region={};
}
Result<RegionIndex> RegionIndex::create(std::span<RegionEntry> entries,std::span<RegionBucket> regions,std::uint32_t maximum) noexcept {
    if(entries.empty()||regions.empty()||!maximum||entries.size()>std::numeric_limits<std::uint32_t>::max()||regions.size()>std::numeric_limits<std::uint32_t>::max()||overlap(std::as_bytes(entries),std::as_bytes(regions)))return fail(Error::InvalidArgument);
    return RegionIndex(entries,regions,maximum);
}
Result<std::uint32_t> RegionIndex::find(ObjectHandle handle) const noexcept {
    if(!handle||handle.slot()>entries_.size())return fail(Error::InvalidArgument);
    const auto& entry=entries_[handle.slot()-1];
    if(!entry.occupied||entry.handle!=handle)return fail(Error::StaleGeneration);
    return handle.slot()-1;
}
void RegionIndex::link(std::uint32_t slot,std::uint32_t region) noexcept {
    auto& entry=entries_[slot];auto& bucket=regions_[region];
    entry.region=region;entry.previous.reset();entry.next=bucket.first;
    if(bucket.first)entries_[*bucket.first].previous=slot;
    bucket.first=slot;++bucket.count;
}
void RegionIndex::unlink(std::uint32_t slot) noexcept {
    auto& entry=entries_[slot];auto& bucket=regions_[entry.region];
    if(entry.previous)entries_[*entry.previous].next=entry.next;else bucket.first=entry.next;
    if(entry.next)entries_[*entry.next].previous=entry.previous;
    --bucket.count;entry.previous.reset();entry.next.reset();
}
Status RegionIndex::insert(ObjectHandle handle,std::uint32_t region) noexcept {
    if(!handle||handle.slot()>entries_.size()||region>=regions_.size())return fail(Error::InvalidArgument);
    auto& entry=entries_[handle.slot()-1];
    if(entry.occupied)return fail(entry.handle==handle?Error::Busy:Error::StaleGeneration);
    if(regions_[region].count>=maximum_per_region_)return fail(Error::CapacityExceeded);
    entry.occupied=true;entry.handle=handle;link(handle.slot()-1,region);return {};
}
Status RegionIndex::move(ObjectHandle handle,std::uint32_t region) noexcept {
    auto slot=find(handle);if(!slot)return fail(slot.error());
    if(region>=regions_.size())return fail(Error::InvalidArgument);
    if(entries_[*slot].region==region)return {};
    // Check destination capacity before touching either list.
    if(regions_[region].count>=maximum_per_region_)return fail(Error::CapacityExceeded);
    unlink(*slot);link(*slot,region);return {};
}
Status RegionIndex::remove(ObjectHandle handle) noexcept {
    auto slot=find(handle);if(!slot)return fail(slot.error());unlink(*slot);entries_[*slot]={};return {};
}
Result<std::size_t> RegionIndex::collect(std::uint32_t region,std::span<ObjectHandle> output) const noexcept {
    if(region>=regions_.size())return fail(Error::InvalidArgument);
    const auto& bucket=regions_[region];
    if(output.size()<bucket.count)return fail(Error::CapacityExceeded);
    if(overlap(std::as_bytes(output),std::as_bytes(entries_))||overlap(std::as_bytes(output),std::as_bytes(regions_)))return fail(Error::InvalidArgument);
    auto slot=bucket.first;std::size_t count=0;std::optional<std::uint32_t> previous{};
    while(slot) {
        // Detect corrupted caller storage instead of looping forever or returning
        // a dangling/recycled handle. Storage remains exclusive to this index.
        if(*slot>=entries_.size()||count>=bucket.count)return fail(Error::ProtocolViolation);
        const auto& entry=entries_[*slot];
        if(!entry.occupied||entry.region!=region||entry.previous!=previous)return fail(Error::ProtocolViolation);
        ++count;previous=slot;slot=entry.next;
    }
    if(count!=bucket.count)return fail(Error::ProtocolViolation);
    slot=bucket.first;count=0;
    while(slot) { const auto& entry=entries_[*slot];output[count++]=entry.handle;slot=entry.next; }
    return count;
}
Result<std::uint32_t> RegionIndex::region(ObjectHandle handle) const noexcept {
    auto slot=find(handle);if(!slot)return fail(slot.error());return entries_[*slot].region;
}
Result<std::size_t> RegionIndex::count(std::uint32_t region) const noexcept {
    if(region>=regions_.size())return fail(Error::InvalidArgument);return regions_[region].count;
}
}
