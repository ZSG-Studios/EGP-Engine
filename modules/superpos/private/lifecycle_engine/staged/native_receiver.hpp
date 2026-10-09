// SPDX-License-Identifier: MIT
#pragma once
// Private C++23 module header. This is not a generated SDK surface.
#include "../weak_factory.hpp"
#include "private/charged_array.hpp"
#include "private/captured_owner.hpp"
#include "core/object/ref_counted.h"

namespace superpos_egp::lifecycle_engine {
class SuperposNativeFactory : public RefCounted, public WeakFactory {
    GDCLASS(SuperposNativeFactory,RefCounted);
public:
    // Shutdown joins/settles native work before engine infrastructure disappears.
    // Admission requires a qualified bounded implementation; no script dispatch.
    virtual superpos::Status quiesce_native() noexcept=0;
protected:
    static void _bind_methods() {}
};
struct Registration {
    superpos::SchemaId schema{};std::uint64_t resource{};std::size_t native_bytes{};
    Ref<SuperposNativeFactory> factory;
};
// The Session's charged owner. Ref pins keep factories alive across deferred
// cancellation, while WeakFactory keeps engine objects weak and generation-safe.
class NativeReceiver {
public:
    explicit NativeReceiver(superpos::Allocator&) noexcept;
    superpos::Status initialize(superpos::Session&,superpos::SchemaRegistry&,std::uint64_t,
        std::span<const Registration>,superpos::ReceiverConfig,superpos::ReplicaSessionRoutes,lifecycle::Limits) noexcept;
    superpos::Result<superpos::ReplicaSessionProgress> pump(superpos::Session&,std::uint64_t,superpos::Tick) noexcept;
    superpos::Status stop() noexcept { return binding_.stop(); }
    bool drained() const noexcept { return binding_.drained(); }
    bool raw_allowed(std::uint32_t channel) const noexcept { return routes_.raw_allowed(channel); }
    superpos::Status progress_retirement(bool shutdown) noexcept;
private:
    std::array<Ref<SuperposNativeFactory>,64> factory_pins_{};
    std::array<Factory,64> factories_{};
    ChargedArray<superpos::ReceiverRecord> receiver_rows_;
    ChargedArray<std::byte> images_,scratch_,wire_;
    ChargedArray<lifecycle::Record> lifecycle_rows_;
    ChargedArray<Job> jobs_;
    ChargedArray<superpos::ReplicaReplySlot> replies_;
    ChargedArray<superpos::PendingReplicaSpawn> pending_;
    std::optional<superpos::PeerReplicaReceiver> receiver_;
    Routes routes_;
    ReceiverBinding binding_;
    bool initialized_{};
};
}
