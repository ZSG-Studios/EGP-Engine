// SPDX-License-Identifier: MIT
#pragma once
#include "core/variant/dictionary.h"
#include <array>
class SuperposSession;
class SuperposReplicaView;
class SuperposSpawner;

// Private native bridge, implemented beside Session::Impl and the shared field
// decoder. Public SDK headers do not expose core C++23 types or borrowed spans.
class SuperposReceiverPublicAccess {
public:
    static Dictionary read(SuperposSession &, uint64_t, uint64_t,
        const std::array<uint64_t, 6> &, const PackedInt64Array *);
    static Error retry(SuperposSession &, uint64_t, uint64_t, const std::array<uint64_t, 6> &);
    static void initialize(SuperposReplicaView &, SuperposSession &, uint64_t,
        uint64_t, const std::array<uint64_t, 6> &);
    static Error attach(SuperposSession &, SuperposSpawner &, const Dictionary &);
    static Error detach(SuperposSession &);
    static void abandon(SuperposSession &) noexcept;
    static Error call_rpc(SuperposSession &, uint64_t, uint64_t, const std::array<uint64_t, 6> &, uint64_t, const PackedByteArray &);
    static Array take_rpcs(SuperposSession &, uint32_t);
    static Dictionary rpc_status(SuperposSession &);
};
