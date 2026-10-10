// SPDX-License-Identifier: MIT
#pragma once
#include "core/object/object_id.h"
#include "core/variant/dictionary.h"
#include "superpos/schema.hpp"
#include <array>
#include <span>
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
    // Gameplay services (histories, predicted spawns). A nonempty canonical
    // span receives an exact copy and must match the schema state size.
    struct Image {
        ObjectID session;
        uint64_t handle = 0, schema = 0, revision = 0, tick = 0, owner = 0, ownership_revision = 0, binding = 0;
        std::array<uint64_t, 6> identity{};
    };
    static Error world_image(SuperposSession &, uint64_t, Image &, std::span<std::byte>);
    // Ready replicas only; a pending replica reports ERR_BUSY.
    static Error replica_image(const SuperposReplicaView &, Image &, std::span<std::byte>);
    static Error schema_fields(SuperposSession &, uint64_t, std::span<superpos::FieldDescriptor>, size_t &, size_t &);
};
