// SPDX-License-Identifier: MIT
#pragma once
#include "canonical_checkpoint.hpp"
#include <algorithm>
namespace superpos::canonical {
// Private bridge-owned native lookup. native_slot NEVER appears on the wire.
// Capture and restore may supply different native slots for the same identity.
// The owner charges full spans and freezes them for this map's lifetime.
struct NativeBinding { std::uint32_t native_slot{}; Identity identity; };
class IdentityMap {
    std::span<const NativeBinding> native_;
    std::span<const std::uint32_t> canonical_;
    IdentityMap(std::span<const NativeBinding> n,std::span<const std::uint32_t> c) noexcept:native_(n),canonical_(c){}
public:
    static Result<IdentityMap> prepare(std::span<const NativeBinding> native,
        std::span<std::uint32_t> canonical_order,std::uint32_t kind) noexcept {
        if(!kind || native.size()>100000 || canonical_order.size()<native.size())return fail(Error::InvalidArgument);
        const auto a=reinterpret_cast<std::uintptr_t>(native.data()), b=reinterpret_cast<std::uintptr_t>(canonical_order.data());
        if(native.size_bytes() && canonical_order.size_bytes() &&
            (a<=b ? b-a<native.size_bytes() : a-b<canonical_order.size_bytes()))return fail(Error::InvalidArgument);
        for(size_t i=0;i<native.size();++i) {
            const auto &v=native[i];
            if(v.identity.kind!=kind || !v.identity.simulation || !v.identity.generation ||
                (i && native[i-1].native_slot>=v.native_slot))return fail(Error::InvalidArgument);
            canonical_order[i]=std::uint32_t(i);
        }
        auto order=canonical_order.first(native.size());
        std::sort(order.begin(),order.end(),[native](auto a,auto b){return native[a].identity.simulation<native[b].identity.simulation;});
        for(size_t i=1;i<order.size();++i)
            if(native[order[i-1]].identity.simulation==native[order[i]].identity.simulation)return fail(Error::InvalidArgument);
        return IdentityMap(native,order);
    }
    // Composition boundaries must reject writes overlapping the frozen map,
    // its bindings or its canonical lookup. This does not extend their lifetime.
    bool overlaps_storage(const void *data, size_t bytes) const noexcept {
        const auto x=reinterpret_cast<std::uintptr_t>(data);
        const auto overlaps=[&](const void *p,size_t n) noexcept {
            const auto y=reinterpret_cast<std::uintptr_t>(p);
            return bytes && n && (x<=y ? y-x<bytes : x-y<n);
        };
        return overlaps(this,sizeof(*this)) || overlaps(native_.data(),native_.size_bytes()) ||
            overlaps(canonical_.data(),canonical_.size_bytes());
    }
    Result<Identity> canonical(std::uint32_t native_slot) const noexcept {
        auto it=std::lower_bound(native_.begin(),native_.end(),native_slot,[](const auto &v,auto slot){return v.native_slot<slot;});
        if(it==native_.end() || it->native_slot!=native_slot)return fail(Error::StaleGeneration);
        return it->identity;
    }
    Result<std::uint32_t> native(Identity identity) const noexcept {
        auto it=std::lower_bound(canonical_.begin(),canonical_.end(),identity.simulation,[this](auto i,auto id){return native_[i].identity.simulation<id;});
        if(it==canonical_.end() || native_[*it].identity!=identity)return fail(Error::StaleGeneration);
        return native_[*it].native_slot;
    }
};
}
