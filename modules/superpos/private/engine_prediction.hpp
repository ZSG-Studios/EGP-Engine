// SPDX-License-Identifier: MIT
#pragma once
#include "../superpos_simulation_provider.h"
#include "charged_array.hpp"
#include "superpos/gameplay.hpp"
#include "superpos/capability.hpp"
#include "superpos/codec.hpp"
#include <cstring>
#include <limits>
#include <optional>

namespace superpos_egp {
// The Session supplies a whole-operation pin/barrier. This predicate verifies
// its original binding/epoch and irreversible owner/reload/close state before
// and after each provider callback, not merely when a public method begins.
struct SimulationGate {
    void *context = nullptr;
    bool (*ready)(void *) noexcept = nullptr;
    bool admitted() const noexcept { return ready && ready(context); }
};

inline superpos::Error simulation_error(Error error) noexcept {
    switch (error) {
        case OK: return superpos::Error::None;
        case ERR_BUSY: return superpos::Error::Busy;
        case ERR_UNCONFIGURED: return superpos::Error::NotReady;
        case ERR_UNAVAILABLE: return superpos::Error::Unsupported;
        case ERR_OUT_OF_MEMORY: return superpos::Error::OutOfMemory;
        case ERR_UNAUTHORIZED: return superpos::Error::PermissionDenied;
        case ERR_PARAMETER_RANGE_ERROR: return superpos::Error::Overflow;
        case ERR_INVALID_PARAMETER: case ERR_INVALID_DATA: return superpos::Error::InvalidArgument;
        default: return superpos::Error::ProtocolViolation;
    }
}

inline bool digest_declared(const uint8_t (&value)[32]) noexcept {
    for (auto byte : value) { if (byte) { return true; } }
    return false;
}

inline superpos::Status validate_simulation_descriptor(const SuperposSimulationDescriptor &value) noexcept {
    if (value.format != SuperposSimulationDescriptor::FORMAT_V1 || value.tick_contract != 1 ||
        value.capabilities != SuperposSimulationDescriptor::DETERMINISTIC_REPLAY) {
        return superpos::fail(superpos::Error::Unsupported);
    }
    if (!value.state_bytes || value.state_bytes > 65536 || value.input_bytes > 4096 ||
        !digest_declared(value.codec_sha256) || !digest_declared(value.rules_sha256) || !digest_declared(value.qualification_sha256)) {
        return superpos::fail(superpos::Error::InvalidArgument);
    }
    return {};
}

inline superpos::Result<superpos::Fingerprint> simulation_fingerprint(
        superpos::CryptographicDigest &digest, const superpos::Fingerprint &schemas,
        const SuperposSimulationDescriptor &descriptor) noexcept {
    if (auto valid = validate_simulation_descriptor(descriptor); !valid) { return superpos::fail(valid.error()); }
    if (!superpos::known_fingerprint(schemas) || digest.algorithm() != superpos::DigestAlgorithm::Sha256) {
        return superpos::fail(superpos::Error::NotReady);
    }
    static constexpr char domain[] = "Superpos-EGP/native-prediction/v1;pure-canonical;integer-ordinal;recovery=none";
    std::array<std::byte, 512> encoding{};
    superpos::Writer writer(encoding);
    auto field = [](const uint8_t (&value)[32]) noexcept {
        return std::span<const std::byte>(reinterpret_cast<const std::byte *>(value), 32);
    };
    if (!writer.raw({reinterpret_cast<const std::byte *>(domain), sizeof(domain) - 1}) ||
        !writer.varuint(descriptor.format) || !writer.varuint(descriptor.state_bytes) ||
        !writer.varuint(descriptor.input_bytes) || !writer.varuint(descriptor.tick_contract) ||
        !writer.varuint(descriptor.capabilities) || !writer.raw(schemas) ||
        !writer.raw(field(descriptor.codec_sha256)) || !writer.raw(field(descriptor.rules_sha256)) ||
        !writer.raw(field(descriptor.qualification_sha256))) {
        return superpos::fail(superpos::Error::ProtocolViolation);
    }
    superpos::Fingerprint result{};
    if (auto hashed = digest.hash(std::span<const std::byte>(encoding).first(writer.size()), result); !hashed) {
        return superpos::fail(hashed.error());
    }
    return result;
}

class NativeSimulationBridge final : public superpos::SimulationAdapter {
    SuperposSimulationProvider *provider;
    SuperposSimulationDescriptor frozen;
    SimulationGate gate;
    superpos::Status finish(Error result) const noexcept {
        if (!gate.admitted()) { return superpos::fail(superpos::Error::NotReady); }
        return result == OK ? superpos::Status{} : superpos::fail(simulation_error(result));
    }
public:
    NativeSimulationBridge(SuperposSimulationProvider *p_provider, SuperposSimulationDescriptor p_frozen, SimulationGate p_gate) noexcept
        : provider(p_provider), frozen(p_frozen), gate(p_gate) {}
    superpos::SimulationDescriptor descriptor() const noexcept override {
        return {frozen.state_bytes, frozen.input_bytes, superpos::SimulationCapability::DeterministicReplay};
    }
    bool admitted() const noexcept { return provider && gate.admitted(); }
    superpos::Status validate_state(std::span<const std::byte> bytes) const noexcept override {
        if (!admitted()) { return superpos::fail(superpos::Error::NotReady); }
        if (bytes.size() != frozen.state_bytes) { return superpos::fail(superpos::Error::InvalidArgument); }
        return finish(provider->validate_state(reinterpret_cast<const uint8_t *>(bytes.data()), uint32_t(bytes.size())));
    }
    superpos::Status validate_input(std::span<const std::byte> bytes) const noexcept override {
        if (!admitted()) { return superpos::fail(superpos::Error::NotReady); }
        if (bytes.size() != frozen.input_bytes) { return superpos::fail(superpos::Error::InvalidArgument); }
        const auto *pointer = bytes.empty() ? nullptr : reinterpret_cast<const uint8_t *>(bytes.data());
        return finish(provider->validate_input(pointer, uint32_t(bytes.size())));
    }
    superpos::Status step(superpos::Tick tick, std::span<const std::byte> state,
            std::span<const std::byte> input, std::span<std::byte> output) noexcept override {
        if (!admitted()) { return superpos::fail(superpos::Error::NotReady); }
        if (state.size() != frozen.state_bytes || input.size() != frozen.input_bytes || output.size() != frozen.state_bytes) {
            return superpos::fail(superpos::Error::InvalidArgument);
        }
        const auto *pointer = input.empty() ? nullptr : reinterpret_cast<const uint8_t *>(input.data());
        return finish(provider->step(tick, reinterpret_cast<const uint8_t *>(state.data()), uint32_t(state.size()),
            pointer, uint32_t(input.size()), reinterpret_cast<uint8_t *>(output.data()), uint32_t(output.size())));
    }
};

inline superpos::MemoryPlan prediction_memory_plan(size_t budget) noexcept {
    superpos::MemoryPlan result;
    result.limits.fill(0);
    result.limits[size_t(superpos::MemoryDomain::History)] = budget;
    return result;
}

// Destruction order is history -> storage -> bridge -> provider -> allocator.
// Construction and every method require the Session's whole-operation barrier.
class NativePredictionStorage {
    superpos::QuotaAllocator allocator;
    Ref<SuperposSimulationProvider> provider;
    SuperposSimulationDescriptor frozen;
    NativeSimulationBridge bridge;
    ChargedArray<superpos::PredictionFrame> frames;
    superpos::Buffer arena;
    superpos::Buffer scratch;
    std::optional<superpos::PredictionHistory> history;
    uint32_t history_ticks = 0;
public:
    NativePredictionStorage(size_t budget, const Ref<SuperposSimulationProvider> &p_provider,
            SuperposSimulationDescriptor p_frozen, SimulationGate gate) noexcept
        : provider(p_provider), frozen(p_frozen),
          bridge(provider.ptr(), frozen, gate), frames(allocator, superpos::MemoryDomain::History),
          arena(allocator, superpos::MemoryDomain::History), scratch(allocator, superpos::MemoryDomain::History) {}
    superpos::Status bind_history(superpos::BudgetAllocator& parent,size_t budget) noexcept { return allocator.bind(parent,prediction_memory_plan(budget)); }
    NativePredictionStorage(const NativePredictionStorage &) = delete;
    NativePredictionStorage &operator=(const NativePredictionStorage &) = delete;
    superpos::Status initialize(superpos::Epoch epoch, uint32_t retained_ticks, superpos::Tick tick,
            uint64_t revision, std::span<const std::byte> initial) noexcept {
        if (!bridge.admitted()) { return superpos::fail(superpos::Error::NotReady); }
        if (history) { return superpos::fail(superpos::Error::Busy); }
        if (auto valid = validate_simulation_descriptor(frozen); !valid) { return valid; }
        if (!retained_ticks || retained_ticks > 4096) { return superpos::fail(superpos::Error::InvalidArgument); }
        const size_t count = size_t(retained_ticks) + 1;
        const size_t stride = size_t(frozen.state_bytes) + frozen.input_bytes;
        if (count > std::numeric_limits<size_t>::max() / stride || count > std::numeric_limits<size_t>::max() / frozen.state_bytes) {
            return superpos::fail(superpos::Error::Overflow);
        }
        if (auto allocated = frames.initialize(count); !allocated) { return allocated; }
        if (!bridge.admitted()) { return superpos::fail(superpos::Error::NotReady); }
        if (auto allocated = arena.resize(count * stride); !allocated) { return allocated; }
        if (!bridge.admitted()) { return superpos::fail(superpos::Error::NotReady); }
        if (auto allocated = scratch.resize(count * frozen.state_bytes); !allocated) { return allocated; }
        if (!bridge.admitted()) { return superpos::fail(superpos::Error::NotReady); }
        auto created = superpos::PredictionHistory::create({epoch, retained_ticks}, bridge,
            frames.span(), arena.bytes(), scratch.bytes(), tick, revision, initial);
        if (!created) { return superpos::fail(created.error()); }
        if (!bridge.admitted()) { return superpos::fail(superpos::Error::NotReady); }
        history.emplace(std::move(*created));
        history_ticks = retained_ticks;
        return {};
    }
    superpos::Status predict(superpos::Epoch epoch, superpos::Tick tick, std::span<const std::byte> input) noexcept {
        if (!bridge.admitted() || !history) { return superpos::fail(superpos::Error::NotReady); }
        return history->predict(epoch, tick, input);
    }
    superpos::Result<superpos::Reconciliation> reconcile(superpos::Epoch epoch, superpos::Tick tick,
            uint64_t revision, std::span<const std::byte> canonical) noexcept {
        if (!bridge.admitted() || !history) { return superpos::fail(superpos::Error::NotReady); }
        return history->reconcile(epoch, tick, revision, canonical);
    }
    superpos::Result<superpos::StateView> current(superpos::Epoch epoch) const noexcept {
        if (!bridge.admitted() || !history) { return superpos::fail(superpos::Error::NotReady); }
        if (history->epoch() != epoch) { return superpos::fail(superpos::Error::StaleEpoch); }
        return history->current();
    }
    superpos::Result<superpos::StateView> at(superpos::Epoch epoch, superpos::Tick tick) const noexcept {
        if (!bridge.admitted() || !history) { return superpos::fail(superpos::Error::NotReady); }
        return history->at(epoch, tick);
    }
    superpos::Result<size_t> pending() const noexcept {
        if (!bridge.admitted() || !history) { return superpos::fail(superpos::Error::NotReady); }
        return history->pending_ticks();
    }
    superpos::Result<uint32_t> capacity() const noexcept {
        if (!bridge.admitted() || !history) { return superpos::fail(superpos::Error::NotReady); }
        return history_ticks;
    }
    superpos::Result<size_t> charged_bytes() const noexcept {
        if (!bridge.admitted() || !history) { return superpos::fail(superpos::Error::NotReady); }
        return allocator.used(superpos::MemoryDomain::History);
    }
};

// The fixed owner/allocator/bridge metadata is charged to the surrounding
// Session quota. Canonical frames and replay bytes use its separate History
// quota. The surrounding Session allocator outlives this move-only owner.
class NativePredictionOwner {
    superpos::Allocator *metadata_allocator = nullptr;
    NativePredictionStorage *storage = nullptr;
public:
    NativePredictionOwner() = default;
    ~NativePredictionOwner() { reset(); }
    NativePredictionOwner(const NativePredictionOwner &) = delete;
    NativePredictionOwner &operator=(const NativePredictionOwner &) = delete;
    NativePredictionOwner(NativePredictionOwner &&other) noexcept
        : metadata_allocator(std::exchange(other.metadata_allocator, nullptr)), storage(std::exchange(other.storage, nullptr)) {}
    NativePredictionOwner &operator=(NativePredictionOwner &&other) noexcept {
        if (this != &other) {
            reset();
            metadata_allocator = std::exchange(other.metadata_allocator, nullptr);
            storage = std::exchange(other.storage, nullptr);
        }
        return *this;
    }
    static superpos::Result<NativePredictionOwner> create(superpos::Allocator &allocator, superpos::BudgetAllocator& parent, size_t history_budget,
            const Ref<SuperposSimulationProvider> &provider, SuperposSimulationDescriptor frozen, SimulationGate gate) noexcept {
        auto *memory = allocator.allocate(sizeof(NativePredictionStorage), alignof(NativePredictionStorage), superpos::MemoryDomain::Session);
        if (!memory) { return superpos::fail(superpos::Error::OutOfMemory); }
        NativePredictionOwner owner;
        owner.metadata_allocator = &allocator;
        owner.storage = std::construct_at(static_cast<NativePredictionStorage *>(memory), history_budget, provider, frozen, gate);
        auto bound=owner.storage->bind_history(parent,history_budget);
        if(!bound)return superpos::fail(bound.error());
        return owner;
    }
    void reset() noexcept {
        auto *previous = std::exchange(storage, nullptr);
        auto *allocator = std::exchange(metadata_allocator, nullptr);
        if (previous) { std::destroy_at(previous); allocator->deallocate(previous); }
    }
    explicit operator bool() const noexcept { return storage != nullptr; }
    NativePredictionStorage *operator->() noexcept { return storage; }
    const NativePredictionStorage *operator->() const noexcept { return storage; }
};
} // namespace superpos_egp
