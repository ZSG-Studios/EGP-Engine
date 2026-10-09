// SPDX-License-Identifier: MIT
#pragma once
#include "native_receiver.hpp"
#include "retirement_queue.hpp"
class SuperposSession;
struct SuperposNativeReceiverAccess {
    static Error attach(SuperposSession&,std::span<const superpos_egp::lifecycle_engine::Registration>,
        superpos::ReceiverConfig,superpos::ReplicaSessionRoutes,superpos_egp::lifecycle::Limits) noexcept;
    static Error shutdown() noexcept;
    static void drain();
    static void orphan(SuperposSession&) noexcept;
    static Error close(SuperposSession&) noexcept;
#ifdef SUPERPOS_LIFECYCLE_FIXTURE
    static void retire_fixture_owner(SuperposSession&) noexcept;
    static Error bind_fixture_transport(SuperposSession&,superpos::TransportProvider&,superpos::SessionConfig) noexcept;
#endif
private:
    static superpos::Status progress(void*,bool) noexcept;
    static void dispose(void*) noexcept;
};
