// SPDX-License-Identifier: MIT
#include "superpos_session.h"
#ifdef SUPERPOS_HAS_RTC
#include "native_rtc_binding.hpp"
#endif
#include "session_access.hpp"
#include "private/lifecycle_engine/staged/native_receiver_access.hpp"
#include "superpos_spawner.h"
#include "private/spawning/receiver_public_access.hpp"
#include "private/spawning/spawn_runtime.hpp"
#if defined(SUPERPOS_HAS_DURABLE_RECOVERY)
#include "private/recovery/native_authority.hpp"
#endif
#include "scene/main/scene_tree.h"
#include "core/object/callable_mp.h"
#include "superpos_world.h"
#include <algorithm>
#include "u64_bits.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/crypto/crypto_core.h"
#include "superpos/allocator.hpp"
#include "superpos/world.hpp"
#include "superpos/registry.hpp"
#include "superpos/session.hpp"
#include "superpos/codec.hpp"
#ifdef SUPERPOS_HAS_DTLS
#include "superpos/dtls.hpp"
#include "superpos/udp.hpp"
#include "superpos/packet_transport.hpp"
#include "superpos/platform_clock.hpp"
#include "superpos/udp_mux.hpp"
#include "private/udp_listener_access.hpp"
#endif
#include "superpos_udp_listener.h"
#include <array>
#include <bit>
#include <memory>
#include <new>
#include <optional>
#include "private/charged_array.hpp"
#include "private/module_memory.hpp"
#include "private/captured_owner.hpp"
#include "private/engine_prediction.hpp"
#include "private/allocation_probe.h"

namespace superpos_egp {
#if defined(SUPERPOS_NATIVE_QUALIFICATION) && defined(DEBUG_ENABLED)
namespace {
AllocationPoint denied_point = AllocationPoint::Count;
AllocationDiagnostics probe_diagnostics;
}
#endif
bool arm_allocation_failure(AllocationPoint point) noexcept {
#if defined(SUPERPOS_NATIVE_QUALIFICATION) && defined(DEBUG_ENABLED)
    if (!Thread::is_main_thread() || point == AllocationPoint::Count || denied_point != AllocationPoint::Count) { return false; }
    denied_point = point;
    return true;
#else
    return false;
#endif
}
bool consume_allocation_failure(AllocationPoint point) noexcept {
#if defined(SUPERPOS_NATIVE_QUALIFICATION) && defined(DEBUG_ENABLED)
    if (!Thread::is_main_thread() || denied_point != point) { return false; }
    denied_point = AllocationPoint::Count;
    ++probe_diagnostics.denied[size_t(point)];
    return true;
#else
    return false;
#endif
}
void allocation_acquired(AllocationPoint point) noexcept {
#if defined(SUPERPOS_NATIVE_QUALIFICATION) && defined(DEBUG_ENABLED)
    ERR_FAIL_COND(!Thread::is_main_thread());
    ++probe_diagnostics.allocated[size_t(point)];
    ++probe_diagnostics.live[size_t(point)];
#endif
}
void allocation_released(AllocationPoint point) noexcept {
#if defined(SUPERPOS_NATIVE_QUALIFICATION) && defined(DEBUG_ENABLED)
    ERR_FAIL_COND(!Thread::is_main_thread());
    --probe_diagnostics.live[size_t(point)];
#endif
}
AllocationDiagnostics allocation_diagnostics() noexcept {
#if defined(SUPERPOS_NATIVE_QUALIFICATION) && defined(DEBUG_ENABLED)
    ERR_FAIL_COND_V(!Thread::is_main_thread(), AllocationDiagnostics{});
    return probe_diagnostics;
#else
    return {};
#endif
}
}
namespace {
class ProbeAllocator final : public superpos::Allocator {
    superpos::Allocator &budget;
    superpos_egp::AllocationPoint point;
public:
    ProbeAllocator(superpos::Allocator &p_budget, superpos_egp::AllocationPoint p_point) noexcept : budget(p_budget), point(p_point) {}
    void *allocate(size_t size, size_t alignment, superpos::MemoryDomain domain) noexcept override {
        if (superpos_egp::consume_allocation_failure(point)) { return nullptr; }
        auto *result = budget.allocate(size, alignment, domain);
        if (result) { superpos_egp::allocation_acquired(point); }
        return result;
    }
    void deallocate(void *pointer) noexcept override {
        if (pointer) { superpos_egp::allocation_released(point); }
        budget.deallocate(pointer);
    }
};
}

namespace {
class EngineSha256 final : public superpos::CryptographicDigest {
public:
    superpos::DigestAlgorithm algorithm() const noexcept override { return superpos::DigestAlgorithm::Sha256; }
    superpos::Status hash(std::span<const std::byte> input, std::span<std::byte, 32> output) noexcept override {
        auto error = CryptoCore::sha256(reinterpret_cast<const unsigned char *>(input.data()), input.size(), reinterpret_cast<unsigned char *>(output.data()));
        return error == OK ? superpos::Status{} : superpos::fail(superpos::Error::AuthenticationFailed);
    }
};
Error translate(superpos::Error value) {
    switch (value) {
        case superpos::Error::None: return OK;
        case superpos::Error::OutOfMemory: case superpos::Error::CapacityExceeded: return ERR_OUT_OF_MEMORY;
        case superpos::Error::NotReady: return ERR_UNCONFIGURED;
        case superpos::Error::Busy: return ERR_BUSY;
        case superpos::Error::PermissionDenied: case superpos::Error::AuthenticationFailed: return ERR_UNAUTHORIZED;
        case superpos::Error::Unsupported: return ERR_UNAVAILABLE;
        case superpos::Error::CounterExhausted: case superpos::Error::Overflow: return ERR_PARAMETER_RANGE_ERROR;
        case superpos::Error::Timeout: return ERR_TIMEOUT;
        case superpos::Error::ChannelFailed: return ERR_CONNECTION_ERROR;
        default: return ERR_INVALID_DATA;
    }
}
uint64_t read_uint(const PackedByteArray &bytes, int &position, int width, bool &ok) {
    if (!ok || position < 0 || width > bytes.size() - position) { ok = false; return 0; }
    uint64_t result = 0;
    for (int i = 0; i < width; ++i) { result |= uint64_t(bytes[position++]) << (8 * i); }
    return result;
}
std::span<const std::byte> byte_view(const PackedByteArray &bytes) {
    return {reinterpret_cast<const std::byte *>(bytes.ptr()), size_t(bytes.size())};
}
superpos::MemoryPlan memory_plan(uint64_t budget) {
    auto result = superpos::MemoryPlan::client();
    result.limits[size_t(superpos::MemoryDomain::World)] = size_t(budget);
    return result;
}
#ifdef SUPERPOS_HAS_DTLS
class EngineClock final : public superpos::Clock {
    std::optional<superpos::PlatformClock> source;
    uint64_t milliseconds = 0;
public:
    superpos::Status initialize() noexcept {
        auto created = superpos::PlatformClock::create();
        if (!created) { return superpos::fail(created.error()); }
        source.emplace(std::move(*created));
        return refresh();
    }
    superpos::Status refresh() noexcept {
        if (!source) { return superpos::fail(superpos::Error::NotReady); }
        auto sampled = source->sample();
        if (!sampled) { return superpos::fail(sampled.error()); }
        const auto next = sampled->continuous_us / 1000;
        if (next < milliseconds) { return superpos::fail(superpos::Error::InvalidArgument); }
        milliseconds = next;
        return {};
    }
    // The owner phase samples the fallible native source before any IO. The
    // legacy carrier Clock interface reads this immutable phase sample.
    uint64_t now_ms() noexcept override { return milliseconds; }
};
class ProvisionedKey final : public superpos::AuthProvider {
    superpos::PeerId peer;
    std::array<std::byte, 32> key{};
public:
    ProvisionedKey() noexcept : peer(0) {}
    ProvisionedKey(superpos::PeerId p_peer, const PackedByteArray &p_key) noexcept : peer(p_peer) { memcpy(key.data(), p_key.ptr(), key.size()); }
    ~ProvisionedKey() {
        volatile std::byte *clear = key.data();
        for (size_t i = 0; i < key.size(); ++i) { clear[i] = std::byte{}; }
    }
    superpos::Status admission_key(superpos::PeerId requested, std::span<std::byte, 32> output) noexcept override {
        if (requested != peer) { return superpos::fail(superpos::Error::PermissionDenied); }
        memcpy(output.data(), key.data(), output.size());
        return {};
    }
};

#endif
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
struct EngineNetwork {
#ifdef SUPERPOS_HAS_DTLS
    superpos::CryptoRuntime crypto{superpos::CryptoOwnership::Borrowed};
    EngineClock clock;
    ProvisionedKey authentication;
    // Single-port server: the listener owning the shared socket is pinned
    // until the port (declared after it, destroyed before it) has detached.
    Ref<SuperposUdpListener> listener;
    std::optional<superpos::UdpSocket> socket;
    std::optional<superpos::RoutedUdpSocket> routed;
    std::optional<superpos::UdpMuxPort> port;
    std::optional<superpos::DtlsAssociation> dtls;
    std::optional<superpos::PacketTransport> packet;
    // Pump cadence (callers pump at their own rate), used to express the carrier's
    // measured retransmission timeout in session ticks.
    uint64_t last_pump_usec = 0;
    uint64_t pump_interval_usec = 0;
    superpos::Tick retry_ticks = 0;

#endif
    std::optional<superpos::Session> session;
    superpos_egp::CapturedOwner<superpos_egp::lifecycle_engine::NativeReceiver> receiver;
#if defined(SUPERPOS_HAS_DURABLE_RECOVERY)
    // Declared after session: destroyed first. It borrows only the Session.
    superpos_egp::CapturedOwner<superpos_egp::recovery::NativeAuthority> authority;
#endif
    ObjectID spawn_runtime;
    uint64_t spawn_binding = 0;
#ifdef SUPERPOS_HAS_RTC
    std::optional<superpos_egp::ProcessHandle> rtc;
#endif
    superpos::Result<superpos_egp::SessionAccess> access() noexcept {
#ifdef SUPERPOS_HAS_RTC
        return superpos_egp::session_access(session,rtc);
#else
        return superpos_egp::session_access(session);
#endif
    }
    superpos::Status pump(superpos_egp::SessionAccess& access,uint64_t generation,superpos::Tick tick) noexcept {
#if defined(SUPERPOS_HAS_DURABLE_RECOVERY)
        // The authority bridge pumps its Session; never pump it twice a tick.
        if(authority){auto result=authority->pump(tick);return result?superpos::Status{}:superpos::fail(result.error());}
#endif
        if(receiver && receiver->drained())return {};
        if(receiver){auto result=receiver->pump(*access.operator->(),generation,tick);return result?superpos::Status{}:superpos::fail(result.error());}
#ifdef SUPERPOS_HAS_DTLS
        if(packet) {
            const uint64_t at=OS::get_singleton()->get_ticks_usec();
            if(last_pump_usec && at>last_pump_usec) {
                const uint64_t sample=std::min<uint64_t>(at-last_pump_usec,1000000);
                pump_interval_usec=pump_interval_usec?(7*pump_interval_usec+sample)/8:sample;
            }
            last_pump_usec=at;
            auto stats=packet->statistics();
            if(stats && stats->retransmit_timeout_us && pump_interval_usec) {
                // Resend only after the measured RTO, never on a fixed tick count.
                const superpos::Tick ticks=(stats->retransmit_timeout_us+pump_interval_usec-1)/pump_interval_usec;
                if(ticks!=retry_ticks && access->set_retry_ticks(ticks))retry_ticks=ticks;
            }
        }
#endif
        return access->pump(tick);
    }
    const char* transport_name() const noexcept {
#ifdef SUPERPOS_HAS_RTC
        if(rtc)return "native_paired_rtc";
#endif
#ifdef SUPERPOS_HAS_DTLS
        if(port)return "native_single_port_udp_dtls12_psk_aes128_gcm";
        if(routed)return "native_routed_udp_dtls12_psk_aes128_gcm";
#endif
        return "native_connected_udp_dtls12_psk_aes128_gcm";
    }
    superpos::Status refresh() noexcept {
#ifdef SUPERPOS_HAS_RTC
        if(rtc)return {};
#endif
#ifdef SUPERPOS_HAS_DTLS
        return clock.refresh();
#else
        return superpos::fail(superpos::Error::NotReady);
#endif
    }
#ifdef SUPERPOS_HAS_RTC
    ~EngineNetwork() {
        if(rtc){
            auto retired=superpos_egp::rtc_process_retire(*rtc);
            if(!retired && retired.error()!=superpos::Error::StaleGeneration)std::abort();
        }
    }
#endif
    superpos::Fingerprint simulation{};
    Error error = OK;
    // Exact core error behind a translated pump failure, for typed diagnostics.
    superpos::Error failure{};
    EngineNetwork() noexcept = default; // RTC borrows the process host; no UDP crypto initialization.
#ifdef SUPERPOS_HAS_DTLS
    EngineNetwork(superpos::PeerId peer, const PackedByteArray &key) noexcept : authentication(peer, key) {}
#endif
    // Destruction is in reverse dependency order: session, packet, DTLS, socket.
};
superpos::Result<superpos::Fingerprint> canonical_profile(EngineSha256 &digest) {
    // The native canonical codec contract is explicitly versioned. It has no
    // input-replay, physics, scene-setter, history, or recovery capability.
    static constexpr char descriptor[] =
        "Superpos-EGP/canonical-state-only/v1;SPGS/v1;SPGO/v1;little-endian;"
        "opaque-u64;schema-native-fields-v1;server-authoritative;"
        "atomic-native-state;scene-projection=none;physics=none;recovery=none";
    std::array<std::byte, 512> encoding{};
    superpos::Writer writer(encoding);
    if (!writer.raw({reinterpret_cast<const std::byte *>(descriptor), sizeof(descriptor) - 1}) ||
        !writer.u64(superpos::World::maximum_group_objects) || !writer.u64(superpos::World::maximum_group_bytes) ||
        !writer.u64(superpos::Schema::maximum_fields) || !writer.u64(65536)) {
        return superpos::fail(superpos::Error::ProtocolViolation);
    }
    superpos::Fingerprint fingerprint{};
    auto hashed = digest.hash(std::span<const std::byte>(encoding).first(writer.size()), fingerprint);
    if (!hashed) { return superpos::fail(hashed.error()); }
    return fingerprint;
}
#endif
}
#ifdef SUPERPOS_HAS_DTLS
// How one association reaches its peer: a connected socket, a client socket
// routed to a single-port server by connection ID, or a server-side port of a
// shared listener.
struct SuperposUdpRoute {
    enum class Kind { Connected, Routed, Listener } kind = Kind::Connected;
    superpos::IpEndpoint local{}, remote{};
    uint64_t connection_id = 0;
    SuperposUdpListener *listener = nullptr;
};
#else
struct SuperposUdpRoute {};
#endif
struct SuperposSession::Impl {
    superpos::BudgetAllocator* metadata_parent;
    superpos::QuotaAllocator allocator;
    ProbeAllocator record_allocator{allocator, superpos_egp::AllocationPoint::Records};
    ProbeAllocator view_allocator{allocator, superpos_egp::AllocationPoint::Views};
    ProbeAllocator arena_allocator{allocator, superpos_egp::AllocationPoint::WorldArena};
    superpos::Buffer arena{arena_allocator, superpos::MemoryDomain::World};
    superpos::WorldSlot *slots = nullptr;
    superpos_egp::ChargedArray<superpos::SchemaRecord> records{record_allocator, superpos::MemoryDomain::Session};
    superpos::RegistryState registry_state;
    TypedArray<SuperposSchema> reload_schemas;
    String reload_authored_baseline;
    EngineSha256 digest;
    std::optional<superpos::SchemaRegistry> registry;
    superpos_egp::ChargedArray<superpos::Schema> schemas{view_allocator, superpos::MemoryDomain::Session};
    std::optional<superpos::RegisteredWorld> world;
    struct PredictionContext { SuperposSession *owner = nullptr; uint64_t binding = 0, epoch = 0; };
    PredictionContext prediction_context;
    superpos_egp::NativePredictionOwner prediction;
    superpos::Fingerprint prediction_fingerprint{};
    uint32_t prediction_history_ticks = 0, prediction_required_history_ticks = 0;
    uint64_t tick = 0, binding_generation = 0, publications = 0;
    uint32_t capacity = 0;
    uint64_t object_generation_floor = 1;
    uint64_t last_physics_frame = 0;
    bool have_physics_frame = false;
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    superpos_egp::lifecycle_engine::RetirementQueue::Node native_retirement;
    ObjectID native_recipient;
    size_t native_slot = 0;
#endif
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    superpos_egp::CapturedOwner<EngineNetwork> network;
#endif
    explicit Impl(superpos::BudgetAllocator& parent) noexcept : metadata_parent(&parent) {}
    static superpos::Result<superpos_egp::CapturedOwner<Impl>> create(uint64_t budget=268435456) noexcept {
        auto valid=superpos_egp::module_memory_configuration();if(!valid)return superpos::fail(valid.error());
        auto object=superpos_egp::captured_create<Impl>(superpos_egp::module_backing(),superpos::MemoryDomain::Session,superpos_egp::module_backing());
        if(!object)return superpos::fail(object.error());
        auto bound=(*object)->allocator.bind(superpos_egp::module_backing(),memory_plan(budget));
        if(!bound)return superpos::fail(bound.error());
        return std::move(*object);
    }
    static void destroy(Impl* previous) noexcept {
        if(previous){auto* parent=previous->metadata_parent;std::destroy_at(previous);parent->deallocate(previous);}
    }
    ~Impl() {
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
        network.reset();
#endif
        prediction.reset();
        world.reset(); if (slots) { allocator.deallocate(slots); }
    }
};
#include "private/session_prediction.inc"
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
#include "private/lifecycle_engine/staged/session_receiver.inc"
#endif

SuperposSession::SuperposSession() { SuperposManagedReload::on_construct(this); auto initial=Impl::create();if(initial)impl=initial->release();else last_error=translate(initial.error()); }
SuperposSession::~SuperposSession() {
    SuperposManagedReload::on_destruct(this);
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    SuperposNativeReceiverAccess::orphan(*this);
#endif
    Impl::destroy(impl);
}
Error SuperposSession::configure(const TypedArray<SuperposSchema> &p_schemas, uint32_t p_max_objects,
        uint64_t p_authority_epoch, uint64_t p_authority_peer, uint64_t p_state_budget) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (closing || simulation_in_flight) { return ERR_BUSY; }
    if (!Thread::is_main_thread()) { return last_error = ERR_UNAVAILABLE; }
    if (!impl) { return last_error = ERR_OUT_OF_MEMORY; }
    if (SuperposManagedReload::is_active()) { return last_error = ERR_BUSY; }
    if (impl->object_generation_floor > UINT32_MAX) { return last_error = ERR_PARAMETER_RANGE_ERROR; }
    if (impl->world) { return last_error = ERR_ALREADY_IN_USE; }
    if (!p_authority_epoch || !p_max_objects || p_max_objects > 100000 || p_schemas.is_empty() || p_schemas.size() > 64 || !p_state_budget || p_state_budget > (512ULL << 20)) { return last_error = ERR_INVALID_PARAMETER; }
    auto prepared=Impl::create(p_state_budget);
    if(!prepared)return last_error=translate(prepared.error());
    auto candidate=std::move(*prepared);
    // Catalog metadata has its own charged Session quota. state_budget bounds
    // World slots and canonical storage, independently of schema complexity.
    auto records = candidate->records.initialize(size_t(p_schemas.size()));
    if (!records) { return last_error = translate(records.error()); }
    auto views = candidate->schemas.initialize(size_t(p_schemas.size()));
    if (!views) { return last_error = translate(views.error()); }
    auto registered = superpos::SchemaRegistry::create(candidate->records.span(), candidate->registry_state, candidate->digest);
    if (!registered) { return last_error = translate(registered.error()); }
    candidate->registry = std::move(*registered);
    std::array<std::byte, superpos::SchemaRegistry::maximum_registry_encoding> fingerprint_scratch{};
    size_t stride = 0;
    for (int s = 0; s < p_schemas.size(); ++s) {
        Ref<SuperposSchema> author = p_schemas[s];
        if (author.is_null()) { return last_error = ERR_INVALID_DATA; }
        Dictionary baked = author->bake();
        Error baked_error = Error(int(baked.get("error", ERR_INVALID_DATA)));
        if (baked_error != OK) { return last_error = baked_error; }
        PackedByteArray bytes = baked["manifest"];
        int position = 8;
        bool ok = bytes.size() >= 28;
        uint64_t schema_id = read_uint(bytes, position, 8, ok);
        uint64_t schema_revision = read_uint(bytes, position, 8, ok);
        auto count = read_uint(bytes, position, 4, ok);
        if (!ok || count > superpos::Schema::maximum_fields) { return last_error = ERR_INVALID_DATA; }
        std::array<superpos::FieldDescriptor, superpos::Schema::maximum_fields> fields{};
        uint32_t offset = 0;
        for (uint64_t f = 0; f < count; ++f) {
            superpos::FieldDescriptor field;
            field.id = read_uint(bytes, position, 8, ok);
            field.kind = static_cast<superpos::FieldKind>(read_uint(bytes, position, 4, ok) - 1);
            field.size = uint32_t(read_uint(bytes, position, 4, ok));
            field.offset = offset;
            offset += field.size;
            field.audience = static_cast<superpos::FieldAudience>(read_uint(bytes, position, 4, ok));
            field.minimum = std::bit_cast<double>(read_uint(bytes, position, 8, ok));
            field.maximum = std::bit_cast<double>(read_uint(bytes, position, 8, ok));
            field.quantization_levels = uint32_t(read_uint(bytes, position, 4, ok));
            auto name_length = read_uint(bytes, position, 4, ok);
            if (!ok || name_length > uint64_t(bytes.size() - position)) { return last_error = ERR_INVALID_DATA; }
            position += int(name_length);
            fields[size_t(f)] = field;
        }
        if (!ok || position != bytes.size()) { return last_error = ERR_INVALID_DATA; }
        auto schema = superpos::Schema::create(schema_id, std::span(fields).first(size_t(count)));
        if (!schema) { return last_error = translate(schema.error()); }
        for (size_t prior = 0; prior < size_t(s); ++prior) { if (candidate->records[prior].schema.id() == schema_id) { return last_error = ERR_INVALID_DATA; } }
        stride = std::max(stride, schema->state_bytes());
        superpos::SchemaContract contract;
        contract.schema_version = schema_revision;
        contract.publication = superpos::PublicationSemantics::ExplicitAtomicGroup;
        contract.group_id = 1; // The SPGO canonical publication domain for this World.
        contract.maximum_group_objects = superpos::World::maximum_group_objects;
        auto added = candidate->registry->add(*schema, contract, fingerprint_scratch);
        if (!added) { return last_error = translate(added.error()); }
        // add() copied descriptors into stable record storage; materialize()
        // fills the separate views only after the complete registry freezes.
    }
    uint64_t slot_bytes = uint64_t(p_max_objects) * sizeof(superpos::WorldSlot);
    uint64_t arena_bytes = uint64_t(p_max_objects) * stride;
    if (slot_bytes + arena_bytes > p_state_budget) { return last_error = ERR_OUT_OF_MEMORY; }
    auto resized = candidate->arena.resize(size_t(arena_bytes));
    if (!resized) { return last_error = translate(resized.error()); }
    candidate->slots = static_cast<superpos::WorldSlot *>(candidate->allocator.allocate(size_t(slot_bytes), alignof(superpos::WorldSlot), superpos::MemoryDomain::World));
    if (!candidate->slots) { return last_error = ERR_OUT_OF_MEMORY; }
    for (uint32_t i = 0; i < p_max_objects; ++i) { new (candidate->slots + i) superpos::WorldSlot(); }
    auto created = superpos::create_registered_world(*candidate->registry, fingerprint_scratch,
        {p_authority_epoch, p_authority_peer, stride}, {candidate->slots, p_max_objects}, candidate->arena.bytes(), candidate->schemas.span());
    if (!created) { return last_error = translate(created.error()); }
    auto generation = superpos::increment(impl->binding_generation);
    if (!generation) { return last_error = translate(generation.error()); }
    // WorldSlot storage belongs to this wrapper. Start every new world above
    // all generations ever used by a previous world in this Session resource.
    candidate->object_generation_floor = impl->object_generation_floor;
    for (uint32_t i = 0; i < p_max_objects; ++i) {
        candidate->slots[i].generation = uint32_t(candidate->object_generation_floor);
    }
    candidate->world = std::move(*created);
    candidate->binding_generation = *generation;
    candidate->capacity = p_max_objects;
    candidate->reload_schemas = p_schemas.duplicate();
    Error tracking = SuperposManagedReload::track(this);
    if (tracking != OK) { return last_error = tracking; }
    Impl::destroy(impl);
    impl = candidate.release();
    impl->reload_authored_baseline = _reload_authored_fingerprint();
    return last_error = OK;
}
Error SuperposSession::close_checked() {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (managed_reload_paused || (closing && !pending_native_retirement) || simulation_in_flight || callbacks_in_flight) { return last_error = ERR_BUSY; }
    if (pending_native_retirement) { return last_error = ERR_BUSY; }
    if (!impl) { return last_error = ERR_OUT_OF_MEMORY; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (auto pending=SuperposNativeReceiverAccess::close(*this);pending!=OK) { return last_error=pending; }
#endif
    auto generation = superpos::increment(impl->binding_generation);
    if (!generation) { return last_error = translate(generation.error()); }
    closing = true;
    Ref<SuperposSession> keep_alive(this);
    struct ClosingScope { bool &flag; ~ClosingScope() { flag = false; } } closing_scope{closing};
    if (impl->world && impl->slots) {
        uint64_t maximum_generation = 0;
        for (uint32_t i = 0; i < impl->capacity; ++i) {
            maximum_generation = std::max(maximum_generation, uint64_t(impl->slots[i].generation));
        }
        impl->object_generation_floor = maximum_generation + 1;
    }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    impl->network.reset();
#endif
    impl->prediction.reset();
    if (owner_retired) { return ERR_UNCONFIGURED; }
    impl->prediction_fingerprint = {};
    impl->prediction_history_ticks = impl->prediction_required_history_ticks = 0;
    impl->world.reset();
    impl->registry.reset();
    impl->records.clear();
    impl->schemas.clear();
    if (impl->slots) { impl->allocator.deallocate(impl->slots); impl->slots = nullptr; }
    impl->arena = superpos::Buffer(impl->allocator, superpos::MemoryDomain::World);
    impl->binding_generation = *generation;
    impl->capacity = 0;
    physics_owner = reload_world_owner = 0;
    last_error = OK;
    SuperposManagedReload::forget(this);
    return OK;
}
void SuperposSession::close() { close_checked(); }
String SuperposSession::get_state() const {
    if (Thread::get_caller_id() != owner_thread) { return "WrongThread"; }
    if (owner_retired) { return "Retired"; }
    if (closing || simulation_in_flight) { return "Closing"; }
    if (!impl || !impl->world) { return "Closed"; }
    if (managed_reload_paused) { return "ManagedReloadPaused"; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (impl->network) {
        if (impl->network->error != OK) { return "NetworkFailed"; }
        auto accessed=impl->network->access();
        if(!accessed){ return "NetworkUnavailable"; }
        return (*accessed)->ready() ? "NetworkReady" : "NetworkConnecting";
    }
#endif
    return "Configured";
}
Error SuperposSession::get_last_error() const { return Thread::get_caller_id() != owner_thread ? ERR_BUSY : owner_retired ? ERR_UNCONFIGURED : last_error; }
Error SuperposSession::query_tick(uint64_t &r_value) const {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (closing || simulation_in_flight) { return ERR_BUSY; }
    if (!impl || !impl->world) { return ERR_UNCONFIGURED; }
    r_value = impl->tick;
    return OK;
}
Dictionary SuperposSession::read_tick() const {
    Dictionary result;
    uint64_t value = 0;
    const Error error = query_tick(value);
    result["error"] = error;
    if (error == OK) { result["value"] = superpos_egp::signed_bits(value); }
    return result;
}
Dictionary SuperposSession::read_authority_epoch() const {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (owner_retired) { result["error"] = ERR_UNCONFIGURED; return result; }
    if (closing || simulation_in_flight) { result["error"] = ERR_BUSY; return result; }
    if (!impl || !impl->world) { return result; }
    auto epoch = impl->world->world().authority_epoch();
    result["error"] = epoch ? OK : translate(epoch.error());
    if (epoch) { result["value"] = superpos_egp::signed_bits(*epoch); }
    return result;
}
Error SuperposSession::query_binding_generation(uint64_t &r_value) const {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (closing || simulation_in_flight) { return ERR_BUSY; }
    if (!impl) { return ERR_UNCONFIGURED; }
    r_value = impl->binding_generation;
    return OK;
}
Dictionary SuperposSession::read_binding_generation() const {
    Dictionary result;
    uint64_t value = 0;
    const Error error = query_binding_generation(value);
    result["error"] = error;
    if (error == OK) { result["value"] = superpos_egp::signed_bits(value); }
    return result;
}
Error SuperposSession::advance_tick() {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (closing || simulation_in_flight) { return ERR_BUSY; }
    if (physics_owner) { return last_error = ERR_BUSY; }
    return step_tick();
}
Error SuperposSession::set_physics_owner(uint64_t p_owner) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (closing || simulation_in_flight) { return ERR_BUSY; }
    if (p_owner && physics_owner && physics_owner != p_owner) { return ERR_ALREADY_IN_USE; }
    physics_owner = p_owner;
    return OK;
}
Error SuperposSession::advance_owned_tick(uint64_t p_owner, uint64_t p_physics_frame) {
    if (Thread::get_caller_id() != owner_thread || !p_owner || p_owner != physics_owner) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (closing || simulation_in_flight || callbacks_in_flight) { return ERR_BUSY; }
    if (!impl || !impl->world) { return last_error = ERR_UNCONFIGURED; }
    if (impl->have_physics_frame && p_physics_frame <= impl->last_physics_frame) { return ERR_BUSY; }
    impl->last_physics_frame = p_physics_frame;
    impl->have_physics_frame = true;
    return step_tick();
}
Error SuperposSession::step_tick() {
    if (owner_retired) { return last_error = ERR_UNCONFIGURED; }
    if (managed_reload_paused || closing || simulation_in_flight || callbacks_in_flight) { return last_error = ERR_BUSY; }
    Ref<SuperposSession> keep_alive(this);
    if (!impl || !impl->world) { return last_error = ERR_UNCONFIGURED; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (impl->network && impl->network->error != OK) { return last_error = impl->network->error; }
#endif
    auto next = superpos::increment(impl->tick);
    if (!next) { return last_error = translate(next.error()); }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (impl->network) {
        auto sampled = impl->network->refresh();
        if (!sampled) {
            impl->network->error = translate(sampled.error());
            return last_error = impl->network->error;
        }
        auto accessed=impl->network->access();
        if(!accessed){ return translate(accessed.error()); }
        superpos::Status pumped;
        ++callbacks_in_flight;
        {
            struct NetworkCallbackGuard { SuperposSession& owner;~NetworkCallbackGuard(){owner._end_callback();} } callbacks{*this};
            pumped = impl->network->pump(*accessed,impl->binding_generation,*next);
        }
        if(owner_retired || !impl)return last_error=ERR_UNCONFIGURED;
        if (!pumped && pumped.error() != superpos::Error::Busy) {
            impl->network->failure = pumped.error();
            impl->network->error = translate(pumped.error());
            return last_error = impl->network->error;
        }
    }
#endif
    impl->tick = *next;
    last_error = OK;
    ++callbacks_in_flight;
    // Only this native tick dispatch admits nested registered prediction.
    // A nested canonical-publication callback has another barrier count and
    // remains unavailable even while this outer phase is active.
    simulation_tick_phase = true;
    emit_signal("simulation_tick", superpos_egp::signed_bits(*next));
    simulation_tick_phase = false;
    _end_callback();
    return owner_retired ? ERR_UNCONFIGURED : OK;
}
Error SuperposSession::configure_udp(bool p_server, const String &p_local_address, uint32_t p_local_port,
        const String &p_remote_address, uint32_t p_remote_port, uint64_t p_session_id,
        uint64_t p_peer_identity, const PackedByteArray &p_admission_key, const Dictionary &p_transport) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (managed_reload_paused || closing || simulation_in_flight) { return last_error = ERR_BUSY; }
    if (!Thread::is_main_thread()) { return last_error = ERR_UNAVAILABLE; }
    if (!impl || !impl->world) { return last_error = ERR_UNCONFIGURED; }
#if !defined(SUPERPOS_HAS_DTLS) || defined(WEB_ENABLED)
    return last_error = ERR_UNAVAILABLE;
#else
    if (impl->network) { return last_error = ERR_ALREADY_IN_USE; }
    if (!p_local_port || p_local_port > 65535 || !p_remote_port || p_remote_port > 65535) { return last_error = ERR_INVALID_PARAMETER; }
    for (const auto &address : {p_local_address, p_remote_address}) {
        if (address.is_empty() || address.length() > 45) { return last_error = ERR_INVALID_PARAMETER; }
        for (int i = 0; i < address.length(); ++i) {
            const char32_t c = address[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == ':' || c == '.')) {
                return last_error = ERR_INVALID_PARAMETER;
            }
        }
    }
    const CharString local_text = p_local_address.utf8(), remote_text = p_remote_address.utf8();
    auto local = superpos::IpEndpoint::parse({local_text.get_data(), size_t(local_text.length())}, uint16_t(p_local_port));
    auto remote = superpos::IpEndpoint::parse({remote_text.get_data(), size_t(remote_text.length())}, uint16_t(p_remote_port));
    if (!local || !remote) { return last_error = ERR_INVALID_PARAMETER; }
    SuperposUdpRoute route;
    route.local = *local;
    route.remote = *remote;
    // A client of a single-port server names its admitted connection ID.
    if (p_transport.has("connection_id")) {
        const Variant connection = p_transport["connection_id"];
        if (p_server || connection.get_type() != Variant::INT || !int64_t(connection)) { return last_error = ERR_INVALID_PARAMETER; }
        route.kind = SuperposUdpRoute::Kind::Routed;
        route.connection_id = uint64_t(int64_t(connection));
    }
    return _configure_udp_route(p_server, route, p_session_id, p_peer_identity, p_admission_key, p_transport);
#endif
}
Error SuperposSession::configure_udp_listener(const Ref<SuperposUdpListener> &p_listener, uint64_t p_connection_id, uint64_t p_session_id,
        uint64_t p_peer_identity, const PackedByteArray &p_admission_key, const Dictionary &p_transport) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (managed_reload_paused || closing || simulation_in_flight) { return last_error = ERR_BUSY; }
    if (!Thread::is_main_thread()) { return last_error = ERR_UNAVAILABLE; }
    if (!impl || !impl->world) { return last_error = ERR_UNCONFIGURED; }
#if !defined(SUPERPOS_HAS_DTLS) || defined(WEB_ENABLED)
    return last_error = ERR_UNAVAILABLE;
#else
    if (impl->network) { return last_error = ERR_ALREADY_IN_USE; }
    if (p_listener.is_null() || !p_listener->is_bound()) { return last_error = ERR_UNCONFIGURED; }
    if (!p_connection_id || p_transport.has("connection_id")) { return last_error = ERR_INVALID_PARAMETER; }
    SuperposUdpRoute route;
    route.kind = SuperposUdpRoute::Kind::Listener;
    route.connection_id = p_connection_id;
    route.listener = p_listener.ptr();
    return _configure_udp_route(true, route, p_session_id, p_peer_identity, p_admission_key, p_transport);
#endif
}
Error SuperposSession::_configure_udp_route(bool p_server, const SuperposUdpRoute &p_route, uint64_t p_session_id,
        uint64_t p_peer_identity, const PackedByteArray &p_admission_key, const Dictionary &p_transport) {
#if !defined(SUPERPOS_HAS_DTLS) || defined(WEB_ENABLED)
    return last_error = ERR_UNAVAILABLE;
#else
    if (!p_session_id || !p_peer_identity || p_admission_key.size() != 32) { return last_error = ERR_INVALID_PARAMETER; }
    bool nonzero_key = false;
    for (int i = 0; i < p_admission_key.size(); ++i) { nonzero_key |= p_admission_key[i] != 0; }
    if (!nonzero_key) { return last_error = ERR_UNAUTHORIZED; }

    // Optional transport profile. Unknown keys are rejected so typos never silently
    // fall back. Channel modes are HELLO-negotiated: both peers must declare them.
    for (const Variant &key : p_transport.keys()) {

        const String name = key;

        if (name != "bundle_frames" && name != "burst_datagrams" && name != "receive_frames" && name != "channel_modes" && name != "minimum_rate" &&
                (name != "connection_id" || p_route.kind != SuperposUdpRoute::Kind::Routed)) { return last_error = ERR_INVALID_PARAMETER; }

    }

    const bool bundle_frames = p_transport.get("bundle_frames", true);

    const int64_t burst_datagrams = p_transport.get("burst_datagrams", 4);

    const int64_t receive_frames = p_transport.get("receive_frames", 64);
    // Real-time budget floor (bytes/s): random loss never starves the stream below it.
    const int64_t minimum_rate = p_transport.get("minimum_rate", 0);

    const Array channel_modes = p_transport.get("channel_modes", Array());

    if (burst_datagrams < 1 || burst_datagrams > 16 || receive_frames < 1 || receive_frames > 64 || channel_modes.size() > 32 || minimum_rate < 0 || minimum_rate > 1073741824) { return last_error = ERR_INVALID_PARAMETER; }

    for (int i = 0; i < channel_modes.size(); ++i) {

        if (channel_modes[i].get_type() != Variant::INT || int64_t(channel_modes[i]) < 0 || int64_t(channel_modes[i]) > int64_t(superpos::DeliveryMode::Unreliable)) { return last_error = ERR_INVALID_PARAMETER; }

    }
    auto prepared=superpos_egp::captured_create<EngineNetwork>(impl->allocator,superpos::MemoryDomain::Session,p_peer_identity,p_admission_key);
    if(!prepared)return last_error=translate(prepared.error());
    auto candidate=std::move(*prepared);
    auto initialized = candidate->crypto.initialize();
    if (!initialized) { return last_error = translate(initialized.error()); }
    auto sampled = candidate->clock.initialize();
    if (!sampled) { return last_error = translate(sampled.error()); }
    auto profile = impl->prediction ? superpos::Result<superpos::Fingerprint>(impl->prediction_fingerprint)
        : canonical_profile(impl->digest);
    if (!profile) { return last_error = translate(profile.error()); }
    candidate->simulation = *profile;
    // Complete UDP payload ceiling 1,200 bytes including any routing prefix.
    superpos::DtlsConfig dtls_config{p_server, p_peer_identity, 15000, 1200, false};
    superpos::DatagramIO *io = nullptr;
    std::span<const std::byte> address_identity;
    uint32_t routing_overhead = 0;
    if (p_route.kind == SuperposUdpRoute::Kind::Listener) {
        // Path validation moves outbound traffic only after an encrypted
        // challenge from the new address; the first handshake binds the path.
        candidate->listener = Ref<SuperposUdpListener>(p_route.listener);
        auto attached = SuperposUdpListenerAccess::attach(*p_route.listener, p_route.connection_id);
        if (!attached) { return last_error = attached.error() == superpos::Error::InvalidArgument ? ERR_ALREADY_IN_USE : translate(attached.error()); }
        candidate->port.emplace(std::move(*attached));
        io = &*candidate->port;
        dtls_config.path_validation = true;
    } else if (p_route.kind == SuperposUdpRoute::Kind::Routed) {
        auto routed = superpos::RoutedUdpSocket::open(p_route.local, p_route.remote, p_route.connection_id);
        if (!routed) { return last_error = translate(routed.error()); }
        candidate->routed.emplace(std::move(*routed));
        io = &*candidate->routed;
        dtls_config.path_validation = true;
        dtls_config.udp_payload_ceiling = 1200 - superpos::UdpMux::routing_bytes;
        routing_overhead = uint32_t(superpos::UdpMux::routing_bytes);
        address_identity = {p_route.remote.address.data(), p_route.remote.length};
    } else {
        auto socket = superpos::UdpSocket::open(p_route.local, p_route.remote);
        if (!socket) { return last_error = translate(socket.error()); }
        candidate->socket.emplace(std::move(*socket));
        io = &*candidate->socket;
        address_identity = {p_route.remote.address.data(), p_route.remote.length};
    }
    auto association = superpos::DtlsAssociation::create(impl->allocator, candidate->clock, *io,
        candidate->authentication, dtls_config, address_identity);
    if (!association) { return last_error = translate(association.error()); }
    candidate->dtls.emplace(std::move(*association));
    const auto checked_epoch = impl->world->world().authority_epoch();
    if (!checked_epoch) { return last_error = translate(checked_epoch.error()); }
    const auto epoch = *checked_epoch;
    // Coalesced packets carry many frames, receipts and a piggybacked ACK per
    // datagram, with a small pacing burst per owner pump.
    superpos::PacketTransportConfig packet_config;

    packet_config.association_epoch = epoch;
    packet_config.routing_overhead_bytes = routing_overhead;

    packet_config.bundle_frames = bundle_frames;

    packet_config.receive_frames = uint8_t(receive_frames);

    packet_config.congestion.burst_datagrams = uint8_t(burst_datagrams);
    packet_config.congestion.minimum_rate_bytes_per_second = uint64_t(minimum_rate);

    auto packet = superpos::PacketTransport::create(impl->allocator, candidate->clock, *candidate->dtls, packet_config);
    if (!packet) { return last_error = translate(packet.error()); }
    candidate->packet.emplace(std::move(*packet));
    superpos::SessionConfig config;
    config.session_id = p_session_id;
    config.epoch = epoch;
    config.local_peer = p_server ? 0 : p_peer_identity;
    config.remote_peer = p_server ? p_peer_identity : 0;
    config.limits.fragment_payload_bytes = 894; // DTLS frame minus packet and delivery envelopes.

    for (int i = 0; i < channel_modes.size(); ++i) { config.channel_modes[i] = superpos::DeliveryMode(int64_t(channel_modes[i])); }
    config.capabilities.schemas = impl->world->fingerprint();
    config.capabilities.simulation = *profile;
    config.capabilities.history_ticks = impl->prediction_history_ticks;
    if (impl->prediction) {
        config.capabilities.capabilities = superpos::Capability::DeterministicReplay | superpos::Capability::Prediction;
        config.admission.capabilities = config.capabilities.capabilities;
        config.admission.history_ticks = impl->prediction_required_history_ticks;
    }
    config.admission.schemas = config.capabilities.schemas;
    config.admission.simulation = config.capabilities.simulation;
    auto session = superpos::Session::create(impl->allocator, *candidate->packet, config);
    if (!session) { return last_error = translate(session.error()); }
    candidate->session.emplace(std::move(*session));
    impl->network = std::move(candidate);
    return last_error = OK;
#endif
}
#ifdef SUPERPOS_HAS_RTC
Error SuperposRtcBindingAccess::bind(SuperposSession& owner,superpos_egp::pairing::Token token) noexcept {
    if(Thread::get_caller_id()!=owner.owner_thread)return ERR_BUSY;
    if(!Thread::is_main_thread())return ERR_UNAVAILABLE;
    if(owner.owner_retired)return ERR_UNCONFIGURED;
    if(owner.managed_reload_paused||owner.closing||owner.simulation_in_flight||owner.callbacks_in_flight)
        return owner.last_error=ERR_BUSY;
    if(!owner.impl||!owner.impl->world)return owner.last_error=ERR_UNCONFIGURED;
#if defined(WEB_ENABLED)
    return owner.last_error=ERR_UNAVAILABLE;
#else
    if(owner.impl->network)return owner.last_error=ERR_ALREADY_IN_USE;
    Ref<SuperposSession> keep_alive(&owner);
    owner.closing=true;
    struct ClosingGuard { SuperposSession& owner; ~ClosingGuard(){owner.closing=false;} } closing{owner};
    ++owner.callbacks_in_flight;
    struct CallbackGuard { SuperposSession& owner; ~CallbackGuard(){owner._end_callback();} } callback{owner};
    auto* const captured_impl=owner.impl;
    const auto binding=captured_impl->binding_generation;
    auto network_backing=superpos_egp::captured_create<EngineNetwork>(captured_impl->allocator,superpos::MemoryDomain::Session);
    if(!network_backing)return owner.last_error=translate(network_backing.error());
    auto candidate=std::move(*network_backing);
    auto profile=captured_impl->prediction?superpos::Result<superpos::Fingerprint>(captured_impl->prediction_fingerprint)
        :canonical_profile(captured_impl->digest);
    if(!profile)return owner.last_error=translate(profile.error());
    candidate->simulation=*profile;
    superpos::SessionConfig config;
    config.capabilities.schemas=captured_impl->world->fingerprint();
    config.capabilities.simulation=*profile;
    config.capabilities.history_ticks=captured_impl->prediction_history_ticks;
    if(captured_impl->prediction){
        config.capabilities.capabilities=superpos::Capability::DeterministicReplay|superpos::Capability::Prediction;
        config.admission.capabilities=config.capabilities.capabilities;
        config.admission.history_ticks=captured_impl->prediction_required_history_ticks;
    }
    config.admission.schemas=config.capabilities.schemas;
    config.admission.simulation=config.capabilities.simulation;
    auto prepared=superpos_egp::rtc_process_prepare(token,config);
    if(!prepared)return owner.last_error=translate(prepared.error());
    if(owner.owner_retired||owner.managed_reload_paused||owner.impl!=captured_impl||
       !captured_impl->world||captured_impl->binding_generation!=binding||captured_impl->network)
        return owner.last_error=ERR_UNCONFIGURED;
    auto attached=superpos_egp::rtc_process_attach(token,*prepared);
    if(!attached)return owner.last_error=translate(attached.error());
    candidate->rtc=*attached; // Every subsequent exit retires through EngineNetwork.
    if(owner.owner_retired||owner.managed_reload_paused||owner.impl!=captured_impl||
       !captured_impl->world||captured_impl->binding_generation!=binding||captured_impl->network)
        return owner.last_error=ERR_UNCONFIGURED;
    captured_impl->network=std::move(candidate); // Non-allocating publication; no user callback.
    return owner.last_error=OK;
#endif
}
#endif
Dictionary SuperposSession::enqueue_packet(const PackedByteArray &p_payload, uint32_t p_channel) {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (owner_retired) { result["error"] = ERR_UNCONFIGURED; return result; }
    if (managed_reload_paused || closing || simulation_in_flight) { result["error"] = ERR_BUSY; return result; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!impl || !impl->network || p_channel >= 32) { return result; }
    if (impl->network->error != OK) { result["error"] = impl->network->error; return result; }
    if(impl->network->receiver && !impl->network->receiver->raw_allowed(p_channel)){ result["error"] = ERR_BUSY; return result; }
#if defined(SUPERPOS_HAS_DURABLE_RECOVERY)

    if(impl->network->authority && !impl->network->authority->raw_allowed(p_channel)){ result["error"] = ERR_BUSY; return result; }
#endif
    auto accessed=impl->network->access();
    if(!accessed){ result["error"]=translate(accessed.error()); return result; }
    auto accepted = (*accessed)->send(byte_view(p_payload), impl->tick, uint8_t(p_channel));
    result["error"] = accepted ? OK : translate(accepted.error());
    if (accepted) {
        result["message"] = superpos_egp::signed_bits(accepted->message);
        result["connection_epoch"] = superpos_egp::signed_bits(accepted->epoch);
        result["binding_generation"] = superpos_egp::signed_bits(impl->binding_generation);
        result["channel"] = p_channel;
        result["outcome"] = "Accepted";
    }
#endif
    return result;
}
Dictionary SuperposSession::read_packet(uint32_t p_channel) const {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (owner_retired) { result["error"] = ERR_UNCONFIGURED; return result; }
    if (closing || simulation_in_flight) { result["error"] = ERR_BUSY; return result; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!impl || !impl->network || p_channel >= 32) { return result; }
    if (impl->network->error != OK) { result["error"] = impl->network->error; return result; }
    auto authority = impl->world->world().authority_epoch();
    if (!authority) { result["error"] = translate(authority.error()); return result; }
    if(impl->network->receiver && !impl->network->receiver->raw_allowed(p_channel)){ result["error"] = ERR_BUSY; return result; }
#if defined(SUPERPOS_HAS_DURABLE_RECOVERY)

    if(impl->network->authority && !impl->network->authority->raw_allowed(p_channel)){ result["error"] = ERR_BUSY; return result; }
#endif
    auto accessed=impl->network->access();
    if(!accessed){ result["error"]=translate(accessed.error()); return result; }
    auto identity = (*accessed)->identity();
    if (!identity) { result["error"] = translate(identity.error()); return result; }
    auto received = (*accessed)->receive(uint8_t(p_channel));
    result["error"] = received ? OK : translate(received.error());
    if (received) {
        PackedByteArray bytes;
        const int requested = superpos_egp::consume_allocation_failure(superpos_egp::AllocationPoint::PacketCopy) ? -1 : int(received->payload.size());
        Error resized = bytes.resize(requested);
        if (resized != OK) { result["error"] = resized; return result; }
        if (!bytes.is_empty()) { memcpy(bytes.ptrw(), received->payload.data(), received->payload.size()); }
        result["payload"] = bytes;
        result["message"] = superpos_egp::signed_bits(received->message);
        result["authority_epoch"] = superpos_egp::signed_bits(*authority);
        result["connection_epoch"] = superpos_egp::signed_bits(identity->connection_epoch);
        result["binding_generation"] = superpos_egp::signed_bits(impl->binding_generation);
        result["channel"] = p_channel;
    }
#endif
    return result;
}
Error SuperposSession::acknowledge_packet(uint64_t p_message, uint64_t p_binding_generation, uint32_t p_channel) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (managed_reload_paused || closing || simulation_in_flight) { return last_error = ERR_BUSY; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!impl || !impl->network) { return last_error = ERR_UNCONFIGURED; }
    if (impl->network->error != OK) { return last_error = impl->network->error; }
    if (!p_message || p_channel >= 32 || p_binding_generation != impl->binding_generation) { return last_error = ERR_INVALID_PARAMETER; }
    if(impl->network->receiver && !impl->network->receiver->raw_allowed(p_channel)){ return last_error = ERR_BUSY; }
#if defined(SUPERPOS_HAS_DURABLE_RECOVERY)

    if(impl->network->authority && !impl->network->authority->raw_allowed(p_channel)){ return last_error = ERR_BUSY; }
#endif
    auto accessed=impl->network->access();
    if(!accessed){ return translate(accessed.error()); }
    auto applied = (*accessed)->applied(p_message, uint8_t(p_channel));
    return last_error = applied ? OK : translate(applied.error());
#else
    return last_error = ERR_UNAVAILABLE;
#endif
}
Dictionary SuperposSession::get_packet_outcome(uint64_t p_message, uint64_t p_binding_generation, uint32_t p_channel) const {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (owner_retired) { result["error"] = ERR_UNCONFIGURED; return result; }
    if (closing || simulation_in_flight) { result["error"] = ERR_BUSY; return result; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!impl || !impl->network) { return result; }
    if (impl->network->error != OK) { result["error"] = impl->network->error; return result; }
    if (!p_message || p_channel >= 32 || p_binding_generation != impl->binding_generation) { result["error"] = ERR_INVALID_PARAMETER; return result; }
    if(impl->network->receiver && !impl->network->receiver->raw_allowed(p_channel)){ result["error"] = ERR_BUSY; return result; }
#if defined(SUPERPOS_HAS_DURABLE_RECOVERY)

    if(impl->network->authority && !impl->network->authority->raw_allowed(p_channel)){ result["error"] = ERR_BUSY; return result; }
#endif
    auto accessed=impl->network->access();
    if(!accessed){ result["error"]=translate(accessed.error()); return result; }
    auto identity = (*accessed)->identity();
    if (!identity) { result["error"] = translate(identity.error()); return result; }
    auto mode = (*accessed)->channel_mode(uint8_t(p_channel));
    if (!mode) { result["error"] = translate(mode.error()); return result; }
    auto outcome = (*accessed)->outcome({identity->connection_epoch, p_message, uint8_t(p_channel), *mode});
    result["error"] = outcome ? OK : translate(outcome.error());
    if (outcome) { result["outcome"] = *outcome == superpos::Outcome::Applied ? "Applied" : *outcome == superpos::Outcome::Received ? "Received" : "Accepted"; }
#endif
    return result;
}
Error SuperposSession::retire_packet(uint64_t p_message, uint64_t p_binding_generation, uint32_t p_channel) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (managed_reload_paused || closing || simulation_in_flight) { return last_error = ERR_BUSY; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!impl || !impl->network) { return last_error = ERR_UNCONFIGURED; }
    if (impl->network->error != OK) { return last_error = impl->network->error; }
    if (!p_message || p_channel >= 32 || p_binding_generation != impl->binding_generation) { return last_error = ERR_INVALID_PARAMETER; }
    if(impl->network->receiver && !impl->network->receiver->raw_allowed(p_channel)){ return last_error = ERR_BUSY; }
#if defined(SUPERPOS_HAS_DURABLE_RECOVERY)

    if(impl->network->authority && !impl->network->authority->raw_allowed(p_channel)){ return last_error = ERR_BUSY; }
#endif
    auto accessed=impl->network->access();
    if(!accessed){ return translate(accessed.error()); }
    auto identity = (*accessed)->identity();
    if (!identity) { return last_error = translate(identity.error()); }
    auto mode = (*accessed)->channel_mode(uint8_t(p_channel));
    if (!mode) { return last_error = translate(mode.error()); }
    auto retired = (*accessed)->retire({identity->connection_epoch, p_message, uint8_t(p_channel), *mode});
    return last_error = retired ? OK : translate(retired.error());
#else
    return last_error = ERR_UNAVAILABLE;
#endif
}
uint64_t SuperposSession::spawn_object(uint64_t p_schema, uint64_t p_owner, const PackedByteArray &p_canonical) {
    if (Thread::get_caller_id() != owner_thread) { return 0; }
    if (owner_retired) { last_error = ERR_UNCONFIGURED; return 0; }
    if (managed_reload_paused || closing || simulation_in_flight) { last_error = ERR_BUSY; return 0; }
    if (!impl || !impl->world) { last_error = ERR_UNCONFIGURED; return 0; }
    auto spawned = impl->world->spawn(p_schema, p_owner, byte_view(p_canonical), impl->tick);
    if (!spawned) { last_error = translate(spawned.error()); return 0; }
    last_error = OK;
    return spawned->value;
}
Error SuperposSession::destroy_object(uint64_t p_handle) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (managed_reload_paused || closing || simulation_in_flight) { return last_error = ERR_BUSY; }
    if (!impl || !impl->world) { return last_error = ERR_UNCONFIGURED; }
    auto epoch = impl->world->world().authority_epoch();
    if (!epoch) { return last_error = translate(epoch.error()); }
    auto result = impl->world->destroy({p_handle}, *epoch);
    return last_error = result ? OK : translate(result.error());
}
Dictionary SuperposSession::read_object(uint64_t p_handle) const {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (owner_retired) { result["error"] = ERR_UNCONFIGURED; return result; }
    if (closing || simulation_in_flight) { result["error"] = ERR_BUSY; return result; }
    if (!impl || !impl->world) { return result; }
    auto entity = impl->world->world().view({p_handle});
    if (!entity) { result["error"] = translate(entity.error()); return result; }
    PackedByteArray bytes;
    const int requested = superpos_egp::consume_allocation_failure(superpos_egp::AllocationPoint::ObjectCopy) ? -1 : int(entity->canonical.size());
    Error resized = bytes.resize(requested);
    if (resized != OK) { result["error"] = resized; return result; }
    if (!bytes.is_empty()) { memcpy(bytes.ptrw(), entity->canonical.data(), entity->canonical.size()); }
    result["error"] = OK;
    result["handle"] = superpos_egp::signed_bits(entity->handle.value);
    result["schema_id"] = superpos_egp::signed_bits(entity->schema->id());
    result["owner"] = superpos_egp::signed_bits(entity->owner);
    result["ownership_revision"] = superpos_egp::signed_bits(entity->ownership_revision);
    result["revision"] = superpos_egp::signed_bits(entity->revision);
    result["tick"] = superpos_egp::signed_bits(entity->tick);
    result["canonical"] = bytes;
    return result;
}
#include "private/session_fields.inc"
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
#include "private/spawning/session_public.inc"
#if defined(SUPERPOS_HAS_DURABLE_RECOVERY)
#include "private/recovery/recovery_access.hpp"
#include "private/recovery/session_recovery.inc"
#endif
#else
Dictionary SuperposReceiverPublicAccess::read(SuperposSession &, uint64_t, uint64_t, const std::array<uint64_t, 6> &, const PackedInt64Array *) { Dictionary r; r["error"] = ERR_UNAVAILABLE; return r; }
Error SuperposReceiverPublicAccess::retry(SuperposSession &, uint64_t, uint64_t, const std::array<uint64_t, 6> &) { return ERR_UNAVAILABLE; }
Error SuperposReceiverPublicAccess::attach(SuperposSession &, SuperposSpawner &, const Dictionary &) { return ERR_UNAVAILABLE; }
Error SuperposReceiverPublicAccess::detach(SuperposSession &) { return ERR_UNAVAILABLE; }
void SuperposReceiverPublicAccess::abandon(SuperposSession &) noexcept {}
#endif
Error SuperposSession::attach_receiver(SuperposSpawner *spawner, const Dictionary &configuration) { return spawner ? SuperposReceiverPublicAccess::attach(*this, *spawner, configuration) : ERR_INVALID_PARAMETER; }
Error SuperposSession::detach_receiver() { return SuperposReceiverPublicAccess::detach(*this); }
Dictionary SuperposSession::read_receiver_status() const {
    Dictionary result; result["error"] = ERR_UNCONFIGURED;
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (impl && impl->network && impl->network->receiver) {
        Ref<superpos_egp::spawning::SuperposSpawnRuntime> runtime(Object::cast_to<superpos_egp::spawning::SuperposSpawnRuntime>(ObjectDB::get_instance(impl->network->spawn_runtime)));
        if (runtime.is_valid()) result = runtime->status();
        result["attached"] = true;
    }
#endif
    return result;
}

Error SuperposSession::publish_packed(const PackedByteArray &p_operations) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (managed_reload_paused || closing || simulation_in_flight) { return last_error = ERR_BUSY; }
    Ref<SuperposSession> keep_alive(this);
    if (!impl || !impl->world) { return last_error = ERR_UNCONFIGURED; }
    if (p_operations.size() < 12 || p_operations.size() > int(superpos::World::maximum_group_bytes + 16 * 20 + 12) || memcmp(p_operations.ptr(), "SPGO", 4)) { return last_error = ERR_INVALID_DATA; }
    int position = 4;
    bool ok = true;
    auto version = read_uint(p_operations, position, 4, ok);
    auto count = read_uint(p_operations, position, 4, ok);
    if (version != 1 || !count || count > superpos::World::maximum_group_objects) { return last_error = ERR_INVALID_DATA; }
    std::array<superpos::Publication, superpos::World::maximum_group_objects> operations{};
    for (size_t i = 0; i < size_t(count); ++i) {
        operations[i].handle.value = read_uint(p_operations, position, 8, ok);
        operations[i].expected_revision = read_uint(p_operations, position, 8, ok);
        auto length = read_uint(p_operations, position, 4, ok);
        if (!ok || length > uint64_t(p_operations.size() - position)) { return last_error = ERR_INVALID_DATA; }
        operations[i].canonical = {reinterpret_cast<const std::byte *>(p_operations.ptr() + position), size_t(length)};
        position += int(length);
    }
    if (!ok || position != p_operations.size()) { return last_error = ERR_INVALID_DATA; }
    auto epoch = impl->world->world().authority_epoch();
    if (!epoch) { return last_error = translate(epoch.error()); }
    auto published = impl->world->publish({operations.data(), size_t(count)}, impl->tick, *epoch);
    if (!published) { return last_error = translate(published.error()); }
    if (impl->publications != UINT64_MAX) { ++impl->publications; }
    auto tick = impl->tick;
    last_error = OK;
    ++callbacks_in_flight;
    emit_signal("canonical_published", superpos_egp::signed_bits(*published), superpos_egp::signed_bits(tick));
    _end_callback();
    return owner_retired ? ERR_UNCONFIGURED : OK;
}
Error SuperposSession::transfer_ownership(uint64_t p_handle, uint64_t p_owner, uint64_t p_expected_revision) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (managed_reload_paused || closing || simulation_in_flight) { return last_error = ERR_BUSY; }
    if (!impl || !impl->world) { return last_error = ERR_UNCONFIGURED; }
    auto epoch = impl->world->world().authority_epoch();
    if (!epoch) { return last_error = translate(epoch.error()); }
    auto changed = impl->world->transfer_ownership({p_handle}, p_owner, p_expected_revision, impl->tick, *epoch);
    return last_error = changed ? OK : translate(changed.error());
}
Dictionary SuperposSession::get_statistics() const {
    Dictionary result;
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (owner_retired) { result["error"] = ERR_UNCONFIGURED; return result; }
    if (closing || simulation_in_flight) { result["error"] = ERR_BUSY; return result; }
    if (!impl) { result["error"] = ERR_UNCONFIGURED; return result; }
    result["error"] = OK;
    if (!impl->world) { result["error"] = ERR_UNCONFIGURED; return result; }
    auto live = impl->world->world().live_count();
    if (!live) { result["error"] = translate(live.error()); return result; }
    result["objects"] = int64_t(*live);
    result["capacity"] = impl->capacity;
    result["tick"] = superpos_egp::signed_bits(impl->tick);
    result["binding_generation"] = superpos_egp::signed_bits(impl->binding_generation);
    result["state_allocated_bytes"] = int64_t(impl->allocator.used(superpos::MemoryDomain::World));
    result["core_allocated_bytes"] = int64_t(impl->allocator.total());
    result["canonical_publications"] = superpos_egp::signed_bits(impl->publications);
    result["transport"] = "not_attached";
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (impl->network) {
        result["transport"] = impl->network->transport_name();
        auto accessed=impl->network->access();
        if(!accessed){ result["error"]=translate(accessed.error()); return result; }
        result["network_ready"] = !managed_reload_paused && impl->network->error == OK && (*accessed)->ready();
        result["network_error"] = impl->network->error;
#ifdef SUPERPOS_HAS_DTLS
        if (impl->network->packet) {
        auto stats = impl->network->packet->statistics();
        if (stats) {
            result["charged_wire_bytes"] = superpos_egp::signed_bits(stats->charged_wire_bytes);
            result["congestion_window"] = superpos_egp::signed_bits(stats->congestion_window);
            result["bytes_in_flight"] = superpos_egp::signed_bits(stats->bytes_in_flight);
            result["smoothed_rtt_us"] = superpos_egp::signed_bits(stats->smoothed_rtt_us);
            result["retransmit_timeout_us"] = superpos_egp::signed_bits(stats->retransmit_timeout_us);
            result["retry_ticks"] = superpos_egp::signed_bits(impl->network->retry_ticks);
        }
        }
#endif
    }
#endif
    result["scene_projection"] = "not_attached";
    return result;
}
bool SuperposSession::is_network_ready() const {
    if (Thread::get_caller_id() != owner_thread || owner_retired || closing || simulation_in_flight || managed_reload_paused) { return false; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!impl || !impl->world || !impl->network || impl->network->error != OK) { return false; }
    auto accessed = impl->network->access();
    return accessed && bool((*accessed)->capabilities());
#else
    return false;
#endif
}
Error SuperposSession::read_raw(uint32_t p_channel, PackedByteArray &r_payload, uint64_t &r_message) const {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (closing || simulation_in_flight) { return ERR_BUSY; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!impl || !impl->network || p_channel >= 32) { return ERR_UNCONFIGURED; }
    if (impl->network->error != OK) { return impl->network->error; }
    if (impl->network->receiver && !impl->network->receiver->raw_allowed(p_channel)) { return ERR_BUSY; }
    auto accessed = impl->network->access();
    if (!accessed) { return translate(accessed.error()); }
    auto received = (*accessed)->receive(uint8_t(p_channel));
    if (!received) { return translate(received.error()); }
    if (r_payload.resize(int64_t(received->payload.size())) != OK) { return ERR_OUT_OF_MEMORY; }
    if (!r_payload.is_empty()) { memcpy(r_payload.ptrw(), received->payload.data(), received->payload.size()); }
    r_message = received->message;
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}
Error SuperposSession::acknowledge_raw(uint64_t p_message, uint32_t p_channel) {
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!impl) { return ERR_UNCONFIGURED; }
    return acknowledge_packet(p_message, impl->binding_generation, p_channel);
#else
    return ERR_UNAVAILABLE;
#endif
}
Error SuperposSession::enqueue_raw(const uint8_t *p_data, size_t p_size, uint32_t p_channel, uint64_t &r_message) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (managed_reload_paused || closing || simulation_in_flight) { return ERR_BUSY; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!impl || !impl->network || p_channel >= 32) { return ERR_UNCONFIGURED; }
    if (impl->network->error != OK) { return impl->network->error; }
    if (impl->network->receiver && !impl->network->receiver->raw_allowed(p_channel)) { return ERR_BUSY; }
    auto accessed = impl->network->access();
    if (!accessed) { return translate(accessed.error()); }
    auto accepted = (*accessed)->send(std::span<const std::byte>(reinterpret_cast<const std::byte *>(p_data), p_size), impl->tick, uint8_t(p_channel));
    if (!accepted) { return translate(accepted.error()); }
    r_message = accepted->message;
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}
Error SuperposSession::retire_raw(uint64_t p_message, uint32_t p_channel) {
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!impl) { return ERR_UNCONFIGURED; }
    return retire_packet(p_message, impl->binding_generation, p_channel);
#else
    return ERR_UNAVAILABLE;
#endif
}
uint64_t SuperposSession::smoothed_rtt_usec() const {
#ifdef SUPERPOS_HAS_DTLS
    if (Thread::get_caller_id() != owner_thread || !impl || !impl->network || !impl->network->packet) { return 0; }
    auto stats = impl->network->packet->statistics();
    return stats ? stats->smoothed_rtt_us : 0;
#else
    return 0;
#endif
}
Dictionary SuperposSession::get_admission_state() const {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    result["ready"] = false;
    result["transport"] = "not_attached";
    result["simulation"] = "unqualified";
    result["recovery"] = "None";
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (owner_retired) { result["error"] = ERR_UNCONFIGURED; return result; }
    if (closing || simulation_in_flight) { result["error"] = ERR_BUSY; return result; }
    if (!impl || !impl->world) { return result; }
    const auto &fingerprint = impl->world->fingerprint();
    result["schemas_sha256"] = String::hex_encode_buffer(reinterpret_cast<const unsigned char *>(fingerprint.data()), fingerprint.size());
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (impl->network) {
        result["transport"] = impl->network->transport_name();
        result["simulation"] = impl->prediction ? "native_prediction_v1" : "canonical_state_only_v1";
        const auto &simulation = impl->network->simulation;
        result["simulation_sha256"] = String::hex_encode_buffer(reinterpret_cast<const unsigned char *>(simulation.data()), simulation.size());
        auto accessed=impl->network->access();
        if(!accessed){ result["error"]=translate(accessed.error()); return result; }
        auto admitted = (*accessed)->capabilities();
        result["ready"] = !managed_reload_paused && impl->network->error == OK && bool(admitted);
        result["error"] = managed_reload_paused ? ERR_BUSY : impl->network->error != OK ? impl->network->error : admitted ? OK : ERR_UNCONFIGURED;
        if (admitted) {
            result["maximum_encoded_bytes"] = admitted->maximum_encoded_bytes;
            result["maximum_decoded_bytes"] = admitted->maximum_decoded_bytes;
            result["history_ticks"] = admitted->history_ticks;
            result["capabilities"] = superpos_egp::signed_bits(uint64_t(admitted->common));
        }
        return result;
    }
#endif
    // A schema freeze is not a qualified simulation or authenticated handshake.
    result["error"] = ERR_UNAVAILABLE;
    return result;
}
void SuperposSession::_bind_methods() {
    ClassDB::bind_method(D_METHOD("attach_receiver", "spawner", "configuration"), &SuperposSession::attach_receiver);
    ClassDB::bind_method(D_METHOD("detach_receiver"), &SuperposSession::detach_receiver);
    ClassDB::bind_method(D_METHOD("read_receiver_status"), &SuperposSession::read_receiver_status);
    ClassDB::bind_method(D_METHOD("configure", "schemas", "max_objects", "authority_epoch", "authority_peer", "state_budget"), &SuperposSession::configure, DEFVAL(4096), DEFVAL(1), DEFVAL(0), DEFVAL(268435456));
    ClassDB::bind_method(D_METHOD("close_checked"), &SuperposSession::close_checked);
    ClassDB::bind_method(D_METHOD("close"), &SuperposSession::close);
    ClassDB::bind_method(D_METHOD("get_state"), &SuperposSession::get_state);
    ClassDB::bind_method(D_METHOD("get_last_error"), &SuperposSession::get_last_error);
    ClassDB::bind_method(D_METHOD("read_tick"), &SuperposSession::read_tick);
    ClassDB::bind_method(D_METHOD("read_authority_epoch"), &SuperposSession::read_authority_epoch);
    ClassDB::bind_method(D_METHOD("read_binding_generation"), &SuperposSession::read_binding_generation);
    ClassDB::bind_method(D_METHOD("advance_tick"), &SuperposSession::advance_tick);
    ClassDB::bind_method(D_METHOD("predict_checked", "binding", "epoch", "tick", "input"), &SuperposSession::predict_checked);
    ClassDB::bind_method(D_METHOD("reconcile_checked", "binding", "epoch", "tick", "revision", "canonical"), &SuperposSession::reconcile_checked);
    ClassDB::bind_method(D_METHOD("read_prediction", "binding", "epoch"), &SuperposSession::read_prediction);
    ClassDB::bind_method(D_METHOD("read_prediction_info", "binding", "epoch"), &SuperposSession::read_prediction_info);
    ClassDB::bind_method(D_METHOD("read_simulation_profile"), &SuperposSession::read_simulation_profile);
    ClassDB::bind_method(D_METHOD("configure_udp", "server", "local_address", "local_port", "remote_address", "remote_port", "session_id", "peer_identity", "admission_key", "transport"), &SuperposSession::configure_udp, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("configure_udp_listener", "listener", "connection_id", "session_id", "peer_identity", "admission_key", "transport"), &SuperposSession::configure_udp_listener, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("enqueue_packet", "payload", "channel"), &SuperposSession::enqueue_packet, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("read_packet", "channel"), &SuperposSession::read_packet, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("acknowledge_packet", "message", "binding_generation", "channel"), &SuperposSession::acknowledge_packet, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("get_packet_outcome", "message", "binding_generation", "channel"), &SuperposSession::get_packet_outcome, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("retire_packet", "message", "binding_generation", "channel"), &SuperposSession::retire_packet, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("spawn_object", "schema", "owner", "canonical"), &SuperposSession::spawn_object);
    ClassDB::bind_method(D_METHOD("destroy_object", "handle"), &SuperposSession::destroy_object);
    ClassDB::bind_method(D_METHOD("read_object", "handle"), &SuperposSession::read_object);
    ClassDB::bind_method(D_METHOD("read_fields", "handle", "fields"), &SuperposSession::read_fields);
    ClassDB::bind_method(D_METHOD("publish_fields", "handle", "expected_revision", "values"), &SuperposSession::publish_fields);
    ClassDB::bind_method(D_METHOD("publish_packed", "operations"), &SuperposSession::publish_packed);
    ClassDB::bind_method(D_METHOD("transfer_ownership", "handle", "owner", "expected_revision"), &SuperposSession::transfer_ownership);
    ClassDB::bind_method(D_METHOD("get_statistics"), &SuperposSession::get_statistics);
    ClassDB::bind_method(D_METHOD("get_admission_state"), &SuperposSession::get_admission_state);
    ClassDB::bind_method(D_METHOD("is_network_ready"), &SuperposSession::is_network_ready);
    MethodInfo tick("simulation_tick", PropertyInfo(Variant::INT, "tick"));
    tick.arguments_metadata.push_back(GodotTypeInfo::METADATA_INT_IS_UINT64);
    ADD_SIGNAL(tick);
    MethodInfo published("canonical_published", PropertyInfo(Variant::INT, "publication"), PropertyInfo(Variant::INT, "tick"));
    published.arguments_metadata.push_back(GodotTypeInfo::METADATA_INT_IS_UINT64);
    published.arguments_metadata.push_back(GodotTypeInfo::METADATA_INT_IS_UINT64);
    ADD_SIGNAL(published);
}

void SuperposSession::_retire_world_owner(uint64_t p_owner) {
    ERR_FAIL_COND(Thread::get_caller_id() != owner_thread);
    if (owner_retired) { _finish_retirement(); return; }
    // Ordinary direct Session close clears reload ownership while the actual
    // World may still own its member reference. That owning destructor can
    // retire it; only a conflicting nonzero owner indicates a routing defect.
    ERR_FAIL_COND(reload_world_owner && reload_world_owner != p_owner);
    // Revoke before releasing any retention or entering native cleanup. No
    // successful counter increment, future tick, or managed reload is needed.
    owner_retired = true;
    physics_owner = reload_world_owner = managed_reload_epoch = 0;
    managed_reload_paused = false;
    last_error = ERR_UNCONFIGURED;
    SuperposManagedReload::forget(this);
    _finish_retirement();
}
void SuperposSession::_finish_retirement() {
    ERR_FAIL_COND(Thread::get_caller_id() != owner_thread);
    if (!owner_retired || retirement_finished || callbacks_in_flight) { return; }
    // Reentrant lifecycle cleanup cannot perform a second release. The
    // caller's ordinary native pin owns this Session until this method exits.
    if(pending_native_retirement)return;
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if(impl && impl->network && impl->network->receiver){SuperposNativeReceiverAccess::orphan(*this);return;}
#endif
    retirement_finished = true;
    if (!impl) { return; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    impl->network.reset();
#endif
    impl->prediction.reset();
    impl->prediction_fingerprint = {};
    impl->prediction_history_ticks = impl->prediction_required_history_ticks = 0;
    impl->world.reset();
    impl->registry.reset();
    impl->records.clear();
    impl->schemas.clear();
    if (impl->slots) { impl->allocator.deallocate(impl->slots); impl->slots = nullptr; }
    impl->arena = superpos::Buffer(impl->allocator, superpos::MemoryDomain::World);
    impl->capacity = 0;
    impl->reload_schemas.clear();
    impl->reload_authored_baseline = String();
    // The permanent flag already rejects every old handle/capsule. Preserve
    // max rather than wrap it or fail to retire because its increment failed.
    if (impl->binding_generation != UINT64_MAX) { ++impl->binding_generation; }
}
void SuperposSession::_end_callback() {
    ERR_FAIL_COND(Thread::get_caller_id() != owner_thread || !callbacks_in_flight);
    --callbacks_in_flight;
    _finish_retirement();
}

Error SuperposSession::_reload_preflight() const {
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if(impl && impl->network && impl->network->receiver)return ERR_BUSY;
#endif
#if defined(SUPERPOS_HAS_DURABLE_RECOVERY)
    if(impl && impl->network && impl->network->authority)return ERR_BUSY;
#endif
    if (!Thread::is_main_thread() || Thread::get_caller_id() != owner_thread) { return ERR_UNAVAILABLE; }
    if (owner_retired) { return ERR_UNAVAILABLE; }
    if (!impl || !impl->world) { return ERR_UNCONFIGURED; }
    if (closing || simulation_in_flight || callbacks_in_flight) { return ERR_BUSY; }
    if (impl->binding_generation == UINT64_MAX) { return ERR_PARAMETER_RANGE_ERROR; }
    const String authored = _reload_authored_fingerprint();
    if (owner_retired || authored != impl->reload_authored_baseline) { return ERR_UNAVAILABLE; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (impl->network) {
        auto accessed=impl->network->access();
        if(!accessed||impl->network->error!=OK||!(*accessed)->ready())return ERR_BUSY;
    }
#endif
    return OK;
}
void SuperposSession::_reload_pause(uint64_t p_epoch) {
    ERR_FAIL_COND(owner_retired || !impl || !impl->world || impl->binding_generation == UINT64_MAX);
    managed_reload_paused = true;
    managed_reload_epoch = p_epoch;
    ++impl->binding_generation;
}
String SuperposSession::_reload_schema_fingerprint() const {
    if (owner_retired) { return {}; }
    if (!impl || !impl->world) { return {}; }
    const auto &fingerprint = impl->world->fingerprint();
    return String::hex_encode_buffer(reinterpret_cast<const unsigned char *>(fingerprint.data()), fingerprint.size());
}
String SuperposSession::_reload_authored_fingerprint() const {
    if (owner_retired) { return {}; }
    if (!impl || impl->reload_schemas.is_empty()) { return {}; }
    TypedArray<SuperposSchema> authors = impl->reload_schemas;
    if (reload_world_owner) {
        const ObjectID identity(reload_world_owner);
        auto *world = Object::cast_to<SuperposWorld>(ObjectDB::get_instance(identity));
        if (!world) { return {}; }
        uint64_t attached_identity = 0;
        const Error checked = world->query_session_identity(attached_identity);
        if (checked != OK || owner_retired || ObjectDB::get_instance(identity) != world || attached_identity != uint64_t(get_instance_id())) { return {}; }
        authors = world->get_schemas();
        if (owner_retired || ObjectDB::get_instance(identity) != world) { return {}; }
    }
    // The authored catalog uses the same bounded admission as configure().
    // Reload comparison never introduces an uncharged, throwing allocation.
    if (authors.is_empty() || authors.size() > 64) { return {}; }
    std::array<std::pair<uint64_t, String>, 64> records{};
    // Non-const Array indexing may copy into its read-only scratch Variant,
    // invoking Resource reference callbacks. Select the const overload.
    const TypedArray<SuperposSchema> &borrowed_authors = authors;
    for (int i = 0; i < borrowed_authors.size(); ++i) {
        // The shared catalog owns every Resource throughout this traversal.
        // Borrow without native reference callbacks into mutable gameplay.
        auto *author = Object::cast_to<SuperposSchema>(static_cast<Object *>(borrowed_authors[i]));
        if (!author) { return {}; }
        String fingerprint = author->get_fingerprint();
        if (owner_retired || fingerprint.length() != 64) { return {}; }
        records[size_t(i)] = {author->get_schema_id(), fingerprint};
    }
    std::sort(records.begin(), records.begin() + authors.size(), [](const auto &a, const auto &b) { return a.first < b.first; });
    std::array<std::byte, 8192> encoding{};
    superpos::Writer writer(encoding);
    for (int i = 0; i < authors.size(); ++i) {
        const auto &record = records[size_t(i)];
        std::array<std::byte, 64> text{};
        for (size_t n = 0; n < text.size(); ++n) {
            const char32_t digit = record.second[int(n)];
            if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'))) { return {}; }
            text[n] = std::byte(uint8_t(digit));
        }
        if (!writer.u64(record.first) || !writer.raw(text)) { return {}; }
    }
    superpos::Fingerprint hash{};
    if (!impl->digest.hash(std::span<const std::byte>(encoding).first(writer.size()), hash)) { return {}; }
    return String::hex_encode_buffer(reinterpret_cast<const unsigned char *>(hash.data()), hash.size());
}
Error SuperposSession::_reload_validate(const SuperposManagedReload::Ticket &p_ticket) const {
    if (owner_retired) { return ERR_UNAVAILABLE; }
    if (!managed_reload_paused || managed_reload_epoch != p_ticket.epoch || !impl || !impl->world ||
            impl->binding_generation != p_ticket.binding || reload_world_owner != p_ticket.world_owner ||
            _reload_schema_fingerprint() != p_ticket.schema_fingerprint ||
            _reload_authored_fingerprint() != p_ticket.authored_fingerprint) { return ERR_UNAVAILABLE; }
    if (owner_retired) { return ERR_UNAVAILABLE; }
    if (impl->prediction) {
        const auto epoch = impl->world->world().authority_epoch();
        if (!epoch) { return translate(epoch.error()); }
        if (impl->prediction_context.owner != this || impl->prediction_context.epoch != *epoch) { return ERR_UNAVAILABLE; }
    }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (impl->network) {
        // Retained ownership is not a reusable time/authority receipt. Sample
        // and run the ordinary live transport gates before native resume.
        auto sampled = impl->network->refresh();
        if (!sampled) { impl->network->error = translate(sampled.error()); return impl->network->error; }
        auto accessed=impl->network->access();
        if(!accessed){ return translate(accessed.error()); }
        auto progressed = impl->network->pump(*accessed,impl->binding_generation,impl->tick);
        if (!progressed && progressed.error() != superpos::Error::Busy) {
            impl->network->error = translate(progressed.error());
            return impl->network->error;
        }
        if (owner_retired) { return ERR_UNAVAILABLE; }
        if (impl->network->error != OK || !(*accessed)->capabilities()) { return ERR_UNCONFIGURED; }
    }
#endif
    return OK;
}
void SuperposSession::_reload_resume(uint64_t p_epoch) {
    if (owner_retired) { return; }
    if (managed_reload_paused && managed_reload_epoch == p_epoch) {
        // The registry validated every participant and now commits without
        // gameplay/reference callbacks. History owns the same canonical state;
        // its gate must adopt the freshly issued binding before it can resume.
        if (impl->prediction) { impl->prediction_context.binding = impl->binding_generation; }
        managed_reload_paused = false; last_error = OK;
    }
}
void SuperposSession::_reload_abort_close() {
    _retire_world_owner(reload_world_owner);
}
