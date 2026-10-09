// Private C++23 participant; never part of generated C++17 SDK headers.
#pragma once
#include "captured_owner.hpp"
#include "superpos/gameplay.hpp"
#include "core/templates/rid.h"
class Box2DPhysicsServer2D;
namespace superpos_egp {
struct Box2DReplayLimits {
    uint32_t bodies = 0, shapes = 0, contacts = 0;
    size_t image_bytes = 0, native_bytes = 0;
};
struct Box2DReplayQualification {
    uint64_t participant_id = 0, schema_version = 0;
    superpos::Fingerprint simulation{}, process_configuration{}, topology{};
    void *context = nullptr;
    // Trusted, pure owner-phase admission: authenticates the locally qualified
    // process/configuration and samples its authority epoch/tick. Supplying a
    // digest alone is insufficient. Excludes engine iteration, topology/config
    // mutation, callbacks/effects and Space destruction through finish_restore.
    bool (*current)(void *, const superpos::Fingerprint &process_configuration,
        const superpos::Fingerprint &topology, superpos::Epoch &, superpos::Tick &) noexcept = nullptr;
};
class Box2DReplayParticipant final : public superpos::RecoveryParticipant {
    struct Storage;
    struct StorageDelete { void operator()(Storage *) const noexcept; };
    using StorageOwner = std::unique_ptr<Storage, StorageDelete>;
    StorageOwner storage;
    superpos::RecoveryParticipantDescriptor frozen{};
    explicit Box2DReplayParticipant(StorageOwner, superpos::RecoveryParticipantDescriptor) noexcept;
public:
    ~Box2DReplayParticipant();
    static superpos::Result<CapturedOwner<Box2DReplayParticipant>> create(
        Box2DPhysicsServer2D *, RID, Box2DReplayLimits, Box2DReplayQualification) noexcept;
    superpos::RecoveryParticipantDescriptor descriptor() const noexcept override { return frozen; }
    superpos::Result<size_t> capture(std::span<std::byte>) const noexcept override;
    superpos::Status stage_restore(superpos::Epoch, superpos::Tick, std::span<const std::byte>) noexcept override;
    void commit_restore() noexcept override;
    void abort_restore() noexcept override;
    // Required after restore_participants returns: retires superseded native
    // worlds outside every participant's non-failing publication callbacks.
    // Space-owned published arena charges continue until Space teardown.
    void finish_restore() noexcept;
};
struct Box2DReplayRestore {
    Box2DReplayParticipant *participant = nullptr;
    std::span<const std::byte> checkpoint;
};
// Bounded local coordinator: complete core all-stage/all-commit transaction,
// then retire every superseded world after all publication callbacks return.
superpos::Status restore_box2d_participants(superpos::Epoch, superpos::Tick,
    std::span<const Box2DReplayRestore>, const superpos::Fingerprint &process_configuration) noexcept;
} // namespace superpos_egp
