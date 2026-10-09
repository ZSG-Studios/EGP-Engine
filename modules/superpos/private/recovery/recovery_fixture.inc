// SPDX-License-Identifier: MIT
// Opt-in native durable-recovery fixture. Included only by the lifecycle
// fixture translation unit when SUPERPOS_HAS_DURABLE_RECOVERY is defined, and
// run only with the explicit --superpos-recovery-fixture user argument.
// Real SQLite journal, native storage worker, coordinator startup/takeover,
// coordinator-minted restore capability and engine AuthorityLease. The
// client link is the explicit in-memory fixture carrier.
#include "private/recovery/restore_publisher.hpp"
#include "superpos_spawner.h"
#include "scene/resources/packed_scene.h"
#include "scene/main/window.h"
#include <superpos/canonical_restore.hpp>
#include <superpos/coordinator_bootstrap.hpp>
#include <superpos/storage_worker.hpp>
#include <superpos/crypto_nonce.hpp>
#include <cstdio>
#include <optional>

namespace superpos_egp::recovery::fixture {
using lifecycle_engine::fixture::checks;
namespace sp = superpos;
namespace svc = superpos::service;

class SuperposRecoveryFixtureNode final : public Node {
    GDCLASS(SuperposRecoveryFixtureNode, Node);
    int value_{}, second_{};
protected:
    static void _bind_methods() {}
    bool _set(const StringName &name, const Variant &value) {
        if (name == StringName("value")) { value_ = int(value); return true; }
        if (name == StringName("second")) { second_ = int(value); return true; }
        return false;
    }
    bool _get(const StringName &name, Variant &value) const {
        if (name == StringName("value")) { value = value_; return true; }
        if (name == StringName("second")) { value = second_; return true; }
        return false;
    }
};

sp::LeaseScope scope(unsigned n) { sp::LeaseScope s{}; s.fill(std::byte(n)); return s; }
struct TestClock final : sp::ClockSource {
    std::uint64_t now{};
    sp::Result<sp::ClockSample> sample() noexcept override { return sp::ClockSample{now, now, 1}; }
};
struct Eligibility final : svc::AuthorityEligibilityProvider {
    sp::Result<svc::AuthorityEligibility> admit(const svc::MatchAuthorityContext &context, const svc::AuthorityRequest &request,
            std::uint64_t generation) noexcept override {
        return svc::AuthorityEligibility{context, request, 1, generation, scope(4), scope(5), scope(6), 1024, 2048, true, true, true, true};
    }
    sp::Status revalidate(const svc::AuthorityEligibility &) noexcept override { return {}; }
};
struct Publisher final : svc::AuthorityGrantPublisher {
    sp::Status send(const sp::AuthorityGrant &, const svc::AuthorityEligibility &) noexcept override { return {}; }
};
struct Capture final : sp::CheckpointParticipantVerifier {
    sp::Fingerprint id{};
    sp::Fingerprint participant_fingerprint() const noexcept override { return id; }
    sp::RecoveryGrade supported_grade() const noexcept override { return sp::RecoveryGrade::PortableRestart; }
    sp::Status verify(const sp::CheckpointManifest &m, sp::CheckpointContentReader &reader) noexcept override {
        std::array<std::byte, sp::checkpoint_chunk_bytes> chunk{}; auto n = reader.read(0, chunk);
        return n && *n == m.bytes ? sp::Status{} : sp::Status(sp::fail(sp::Error::RecoveryUnavailable));
    }
};
template <class T> bool error_is(const T &result, sp::Error error) { return !result && result.error() == error; }
// In-memory datagram carrier. When armed, drops about a quarter of the
// packets it sends, so core delivery must retransmit. A fixed-seed generator
// keeps runs reproducible; a strict period could phase-lock with retransmits
// and starve one message forever, which real loss does not do.
struct LossyCarrier final : sp::TransportProvider {
    LossyCarrier *remote{}; std::array<lifecycle_engine::fixture::Packet, 64> packets{}; std::size_t head{}, count{};
    bool armed{}; std::uint32_t sends{}, dropped{}; std::uint64_t state{0x9e3779b97f4a7c15ull};
    sp::TransportCapabilities capabilities() const noexcept override { return {true, true, true, false, 960}; }
    bool ready() const noexcept override { return true; }
    sp::Status advance() noexcept override { return {}; }
    sp::Status send(std::span<const std::byte> bytes) noexcept override {
        if (bytes.size() > 960 || remote->count == 64) return sp::fail(sp::Error::CapacityExceeded);
        ++sends;
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        if (armed && (state >> 33) % 4 == 0) { ++dropped; return {}; }
        auto &packet = remote->packets[(remote->head + remote->count) % 64];
        std::copy(bytes.begin(), bytes.end(), packet.bytes.begin()); packet.size = bytes.size(); ++remote->count; return {};
    }
    sp::Result<std::size_t> receive(std::span<std::byte> bytes) noexcept override {
        if (!count) return sp::fail(sp::Error::Busy); auto &packet = packets[head]; if (bytes.size() < packet.size) return sp::fail(sp::Error::Truncated);
        std::copy_n(packet.bytes.begin(), packet.size, bytes.begin()); const auto size = packet.size; head = (head + 1) % 64; --count; return size;
    }
};
// AuthorityLease is immovable: its Result is constructed in place here.
struct LeaseHolder {
    sp::Result<sp::AuthorityLease> made;
    LeaseHolder(sp::ClockSource &source, sp::ContinuityGuard &guard, sp::LeaseNonceProvider &nonce) noexcept
        : made(sp::AuthorityLease::create(source, guard, nonce)) {}
};

struct Run {
    // Producer; restored world owner (no transport); four authority-side link
    // Sessions, each paired with one client Session and spawner. Link 1 is
    // lossy; link 3's client joins after the first three have converged.
    static constexpr unsigned clients_count = 4, lossy = 1, late = 3;
    Ref<SuperposSession> producer, authority;
    std::array<Ref<SuperposSession>, clients_count> links, clients;
    std::array<std::array<LossyCarrier, 2>, clients_count> wires;
    std::array<SuperposSpawner *, clients_count> spawners{};
    std::array<bool, clients_count> bound{}, converged_at{};
    Ref<SuperposSpawnCatalog> catalog;
    TypedArray<SuperposSchema> schemas;
    sp::CryptoRuntime crypto{sp::CryptoOwnership::Borrowed};
    std::optional<sp::PsaSha256> digest, worker_digest;
    std::optional<sp::PsaLeaseNonce> nonce;
    sp::BudgetAllocator allocator;
    std::optional<svc::NativeStorageWorker> worker;
    TestClock source;
    sp::ContinuityGuard clock;
    Eligibility eligibility;
    std::optional<svc::CoordinatorBootstrap> bootstrap;
    std::optional<svc::AuthorityCoordinator> coordinator;
    std::optional<LeaseHolder> lease;
    sp::AuthorityLease &gate() { return *lease->made; }
    std::optional<svc::RestoreReadCapability> capability;
    sp::AuthorityGrant grant{};
    sp::CheckpointTicket verified{};
    sp::Fingerprint schemas_fp{}, simulation_fp{}, participants_fp{}, codec_fp{}, final_state{};
    std::array<std::byte, 4096> final_payload{};
    std::size_t final_bytes{};
    std::uint32_t expected_objects{};
    RestoreFanout fanout;
    std::uint64_t frame_ms{};
    char filename[1024]{};
    unsigned stage{}, frames{}, dropped{};
    bool failed{};
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);

    sp::Fingerprint hash(std::span<const std::byte> bytes) { sp::Fingerprint f{}; (void)digest->hash(bytes, f); return f; }
    sp::Fingerprint label(const char *text) { return hash(std::as_bytes(std::span(text, std::strlen(text)))); }
    void observe_to(std::uint64_t target) { while (source.now < target) { source.now += std::min<std::uint64_t>(250000, target - source.now); (void)clock.observe(source); } }
    template <class Done> bool until(Done done) {
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!done()) { if (std::chrono::steady_clock::now() >= limit) return false; std::this_thread::yield(); }
        return true;
    }
    bool drive(svc::CanonicalRestore &restore, svc::CanonicalRestorePhase target, sp::Result<svc::CanonicalRestoreProgress> &last) {
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for (;;) {
            last = restore.poll();
            if (last && (last->phase == target || last->phase == svc::CanonicalRestorePhase::Sealed)) return true;
            if (!last && last.error() != sp::Error::Busy) return true;
            if (std::chrono::steady_clock::now() >= limit) return false;
            std::this_thread::yield();
        }
    }
    bool configure(Ref<SuperposSession> &session, uint64_t epoch, uint64_t peer) {
        session.instantiate();
        VERIFY(session->configure(schemas, 8, epoch, peer, 4 << 20) == OK);
        return true;
    }
    PackedByteArray state(int value, int second) {
        PackedByteArray bytes; bytes.resize(8);
        auto *p = bytes.ptrw(); std::memset(p, 0, 8);
        for (int i = 0; i < 4; ++i) { p[i] = uint8_t(uint32_t(value) >> (8 * i)); p[4 + i] = uint8_t(uint32_t(second) >> (8 * i)); }
        return bytes;
    }
    sp::Result<std::size_t> capture(SuperposSession &session, std::span<std::byte> out) { return SuperposRecoveryAccess::capture(session, out); }
    bool envelope(sp::CanonicalStateHeader header, std::span<const std::byte> payload, std::span<std::byte> out, std::size_t &size) {
        auto encoded = sp::encode_canonical_state(header, payload, out); VERIFY(encoded); size = *encoded; return true;
    }

    // Previous coordinator incarnation: the producing host's canonical world
    // journal. Full record at 1, verified checkpoint anchor at 2, fence, then
    // a final full record at 3 in epoch 2. The final world has a destroyed
    // slot, so a sequential restore cannot preserve every handle.
    bool produce() {
        Ref<SuperposSchema> schema; schema.instantiate(); schema->set_schema_id(19);
        TypedArray<SuperposField> fields;
        for (unsigned id = 1; id <= 2; ++id) { Ref<SuperposField> field; field.instantiate(); field->set_field_id(id); field->set_codec_id(4); field->set_max_bytes(4); fields.append(field); }
        schema->set_fields(fields); schemas.append(schema);
        VERIFY(configure(producer, 1, 1));
        auto target = SuperposRecoveryAccess::target(*producer.ptr()); VERIFY(target);
        schemas_fp = target->schemas_fingerprint; simulation_fp = target->simulation_fingerprint;
        participants_fp = label("superpos-egp-world-participant-v1"); codec_fp = label("superpos-egp-world-payload-v1");
        std::array<uint64_t, 4> handles{};
        for (int i = 0; i < 3; ++i) { handles[i] = producer->spawn_object(19, 2, state(10 + i, 100 + i)); VERIFY(handles[i] != 0); }
        std::array<std::byte, 4096> p1{}, p2{}, p3{};
        auto n1 = capture(*producer.ptr(), p1); VERIFY(n1);
        handles[3] = producer->spawn_object(19, 2, state(13, 103)); VERIFY(handles[3] != 0);
        auto n2 = capture(*producer.ptr(), p2); VERIFY(n2);
        VERIFY(producer->destroy_object(handles[1]) == OK);
        auto n3 = capture(*producer.ptr(), p3); VERIFY(n3);
        std::memcpy(final_payload.data(), p3.data(), *n3); final_bytes = *n3; expected_objects = 3;
        const auto s1 = std::span<const std::byte>(p1.data(), *n1), s2 = std::span<const std::byte>(p2.data(), *n2), s3 = std::span<const std::byte>(p3.data(), *n3);
        final_state = hash(s3);
        path(filename, "superpos-recovery-fixture.sqlite");
        auto store = sp::SqliteJournal::open(allocator, filename, {13}); VERIFY(store);
        const auto old = label("previous coordinator incarnation");
        VERIFY(store->begin_coordinator_boot({}, old)); VERIFY(store->bind_coordinator_boot({0, old}));
        VERIFY(store->bind_checkpoint_digest(*digest)); VERIFY(store->fence(0, 1));
        sp::CanonicalStateClaim claim{sp::CanonicalStateKind::FullRecord, schemas_fp, simulation_fp, participants_fp, codec_fp, label("genesis"), hash(s1)};
        VERIFY(store->append_canonical(1, 1, 1, claim, s1));
        claim.predecessor = claim.result; claim.result = hash(s2); VERIFY(store->append_canonical(1, 2, 2, claim, s2));
        auto service = sp::checkpoint_service_codec(*digest); VERIFY(service);
        sp::CheckpointManifest manifest{13, 1, 1, 2, 2, 0, schemas_fp, simulation_fp, participants_fp, *service, {}, sp::RecoveryGrade::PortableRestart};
        sp::CanonicalStateHeader anchor{sp::CanonicalStateKind::Checkpoint, {13, 1, 2, 2}, schemas_fp, simulation_fp, participants_fp, codec_fp, false, {}, hash(s2)};
        std::array<std::byte, 4096 + 256> content{}; std::size_t bytes = 0;
        VERIFY(envelope(anchor, s2, content, bytes));
        const auto stored = std::span<const std::byte>(content.data(), bytes);
        manifest.bytes = bytes; std::array<sp::Fingerprint, 1> chunks{hash(stored)};
        auto root = sp::checkpoint_content_commitment(manifest, chunks, *digest); VERIFY(root); manifest.content = *root;
        auto begun = store->begin_checkpoint(manifest); VERIFY(begun);
        VERIFY(store->put_checkpoint_chunk(begun->ticket, 0, stored)); VERIFY(store->seal_checkpoint(begun->ticket));
        auto pages = store->begin_checkpoint_service(begun->ticket); VERIFY(pages);
        while (!pages->complete) { pages = store->capture_checkpoint_service_page(begun->ticket, pages->pages); VERIFY(pages); }
        Capture verifier; verifier.id = participants_fp;
        VERIFY(store->verify_checkpoint_recovery(begun->ticket, verifier));
        verified = begun->ticket;
        VERIFY(store->fence(1, 2));
        claim.predecessor = claim.result; claim.result = final_state; VERIFY(store->append_canonical(2, 3, 3, claim, s3));
        VERIFY(producer->close_checked() == OK); producer.unref();
        return true;
    }
    static void path(char (&result)[1024], const char *name) {
        const CharString base = OS::get_singleton()->get_cache_path().path_join(name).utf8();
        std::snprintf(result, sizeof(result), "%s", base.get_data());
        std::remove(result); char extra[1100]{};
        for (const char *suffix : {"-wal", "-shm"}) { std::snprintf(extra, sizeof(extra), "%s%s", result, suffix); std::remove(extra); }
    }
    svc::AuthorityRequest request(svc::GrantIntent intent, const sp::LeaseRequest &lease_request) {
        return {1, sp::AuthorityKind::Dedicated, intent, scope(9), 11, 1, 1, lease_request};
    }
    // Native worker, fresh coordinator startup, takeover with the engine lease.
    bool takeover() {
        auto started = svc::NativeStorageWorker::create(allocator, filename, {{13}, {4, 2 * 1024 * 1024}, &*worker_digest}); VERIFY(started);
        worker.emplace(std::move(*started));
        VERIFY(until([&] { auto phase = worker->phase(); return phase && (*phase == svc::StorageWorkerPhase::Running || *phase == svc::StorageWorkerPhase::Failed); }));
        VERIFY(*worker->phase() == svc::StorageWorkerPhase::Running);
        auto queue = worker->queue(); VERIFY(queue);
        bootstrap.emplace(**queue, *nonce); VERIFY(bootstrap->start());
        VERIFY(until([&] { (void)bootstrap->poll(); return bootstrap->phase() == svc::BootstrapPhase::Ready; }));
        auto boot = bootstrap->identity(); VERIFY(boot);
        VERIFY(clock.revalidate({0, 0, 1}, 1, 0));
        coordinator.emplace(**queue, source, clock, eligibility, svc::MatchAuthorityContext{13, scope(1), scope(2), scope(3), *boot});
        VERIFY(coordinator->start());
        VERIFY(until([&] { (void)coordinator->poll(); return coordinator->phase() == svc::CoordinatorPhase::Ready; }));
        observe_to(2300000);
        lease.emplace(source, clock, *nonce); VERIFY(lease->made);
        auto ask = gate().begin_request(); VERIFY(ask);
        VERIFY(coordinator->request(request(svc::GrantIntent::Takeover, *ask)));
        VERIFY(until([&] { (void)coordinator->poll(); return coordinator->phase() == svc::CoordinatorPhase::Confirmed; }));
        auto minted = coordinator->restore_read(verified, 16); VERIFY(minted); capability.emplace(*minted);
        grant = capability->scope().authority.proposal.grant;
        VERIFY(grant.epoch == 3 && capability->scope().authority.fenced_prefix.sequence == 3);
        VERIFY(gate().authenticated_grant(grant));
        VERIFY(gate().phase() == sp::LeasePhase::AwaitingRestore);
        return true;
    }
    // Every link and client uses the successor epoch; client spawners rebind
    // there, so views from an older epoch are stale.
    bool bind_pair(unsigned i) {
        VERIFY(configure(clients[i], grant.epoch, 2));
        wires[i][0].remote = &wires[i][1]; wires[i][1].remote = &wires[i][0];
        std::array<Ref<SuperposSession>, 2> pair{links[i], clients[i]};
        for (unsigned side = 0; side < 2; ++side) {
            sp::SessionConfig config; config.session_id = 300 + i; config.epoch = 8;
            config.local_peer = side + 1; config.remote_peer = 2 - side; config.logical_channels = 3;
            config.channel_modes[0] = sp::DeliveryMode::ReliableOrdered;
            config.channel_modes[1] = config.channel_modes[2] = sp::DeliveryMode::ReliableUnordered;
            config.channel_purposes[0] = sp::ChannelPurpose::Control;
            VERIFY(SuperposNativeReceiverAccess::bind_fixture_transport(*pair[side].ptr(), wires[i][side], config) == OK);
        }
        bound[i] = true;
        return true;
    }
    bool sessions() {
        VERIFY(configure(authority, grant.epoch, 1));
        for (unsigned i = 0; i < clients_count; ++i) VERIFY(configure(links[i], grant.epoch, 1));
        for (unsigned i = 0; i < clients_count; ++i) if (i != late) VERIFY(bind_pair(i));
        return true;
    }
    bool ready(unsigned i) {
        return bound[i] && links[i]->get_state() == String("NetworkReady") && clients[i]->get_state() == String("NetworkReady");
    }
    sp::Result<std::size_t> authority_live() { return SuperposRecoveryAccess::live_count(*authority.ptr()); }
    sp::Result<sp::AllocatedOwner<svc::CanonicalRestore>> restore_for(const svc::RestoreReadCapability &cap, WorldParticipant &participant) {
        return svc::create_canonical_restore(allocator, **worker->queue(), cap, participant, *digest, source, clock);
    }
    // WorldParticipant is immovable: construct it in place in the caller.
    bool participant_for(SuperposSession &target, std::optional<WorldParticipant> &participant) {
        auto bound = SuperposRecoveryAccess::target(target); if (!bound) return false;
        participant.emplace(bound->schemas, bound->capacity, bound->state_stride, *bound->digest,
            sp::HostRestoreCapabilities{schemas_fp, simulation_fp, participants_fp, sp::RecoveryGrade::PortableRestart,
                sp::checkpoint_maximum_bytes, 1024 * 1024, 65536, true});
        return true;
    }

    // Negatives that must leave the target Session and lease untouched,
    // then renewal, the stale superseded capability, and the real restore.
    bool restore() {
        for (unsigned i = 0; i < clients_count; ++i) VERIFY(fanout.add_link(*links[i].ptr(), {{grant.epoch, 8, 9}, 2, 8, 8}).value_or(99) == i);
        // Before the seal and lease restoration nothing is attached or sent.
        VERIFY(error_is(fanout.step(*authority.ptr(), gate(), grant, frame_ms), sp::Error::NotReady));
        for (unsigned i = 0; i < clients_count; ++i) { auto link = fanout.status(i); VERIFY(link && !link->attached); }
        std::optional<WorldParticipant> participant; VERIFY(participant_for(*authority.ptr(), participant));
        {
            auto made = restore_for(*capability, *participant); VERIFY(made); auto &driver = **made;
            VERIFY(driver.start());
            sp::Result<svc::CanonicalRestoreProgress> last = sp::fail(sp::Error::NotReady);
            VERIFY(drive(driver, svc::CanonicalRestorePhase::Records, last) && last && participant->active());
            VERIFY(driver.abort() && !participant->active());
            VERIFY(driver.poll()->phase == svc::CanonicalRestorePhase::Aborted);
            VERIFY(error_is(participant->sealed_payload(), sp::Error::NotReady));
            VERIFY(authority_live().value_or(99) == 0 && gate().phase() == sp::LeasePhase::AwaitingRestore);
        }
        // Renewal of the same lineage supersedes the first capability.
        const svc::RestoreReadCapability superseded = *capability;
        Publisher announce; VERIFY(coordinator->publish(announce));
        auto ask = gate().begin_request(); VERIFY(ask);
        VERIFY(coordinator->request(request(svc::GrantIntent::Renew, *ask)));
        VERIFY(until([&] { (void)coordinator->poll(); return coordinator->phase() == svc::CoordinatorPhase::Confirmed; }));
        auto renewed = coordinator->restore_read(verified, 16); VERIFY(renewed);
        VERIFY(renewed->scope().authority.proposal.grant.grant_sequence > grant.grant_sequence);
        grant = renewed->scope().authority.proposal.grant; capability.emplace(*renewed);
        VERIFY(gate().authenticated_grant(grant) && gate().phase() == sp::LeasePhase::AwaitingRestore);
        {
            auto made = restore_for(superseded, *participant); VERIFY(made); VERIFY((*made)->start());
            sp::Result<svc::CanonicalRestoreProgress> last = sp::fail(sp::Error::NotReady);
            VERIFY(drive(**made, svc::CanonicalRestorePhase::Sealed, last) && error_is(last, sp::Error::StaleGeneration));
            VERIFY(!participant->active() && authority_live().value_or(99) == 0);
        }
        // The current capability: plan, checkpoint, service pages, records, seal.
        auto made = restore_for(*capability, *participant); VERIFY(made); auto &driver = **made;
        VERIFY(driver.start());
        sp::Result<svc::CanonicalRestoreProgress> last = sp::fail(sp::Error::NotReady);
        VERIFY(drive(driver, svc::CanonicalRestorePhase::Sealed, last) && last && last->phase == svc::CanonicalRestorePhase::Sealed);
        VERIFY(last->chunks == 1 && last->records == 1 && participant->records() == 1);
        auto plan = driver.sealed_plan(); VERIFY(plan && plan->final_state == final_state && plan->successor_epoch == grant.epoch);
        auto sealed = participant->sealed_payload(); VERIFY(sealed && sealed->size() == final_bytes);
        VERIFY(std::memcmp(sealed->data(), final_payload.data(), final_bytes) == 0);
        // Apply, then lease restoration for the exact authority/membership.
        std::array<HandleMapping, 8> mapping{};
        auto applied = SuperposRecoveryAccess::apply(*authority.ptr(), *sealed, plan->successor_epoch, mapping);
        if (!applied) { std::printf("RECOVERY_APPLY_ERROR error=%u live=%d\n", unsigned(applied.error()), int(authority_live().value_or(-1))); std::fflush(stdout); }
        VERIFY(applied);
        VERIFY(applied->objects == expected_objects && !applied->handles_preserved && mapping[1].original != mapping[1].restored);
        VERIFY(error_is(SuperposRecoveryAccess::apply(*authority.ptr(), *sealed, plan->successor_epoch, mapping), sp::Error::Busy));
        VERIFY(driver.abort() && !participant->active());
        VERIFY(gate().restored(grant.coordinator_term, grant.epoch, grant.membership_generation, grant.owner, grant.kind));
        VERIFY(gate().phase() == sp::LeasePhase::Active);
        // A different authority/kind is refused by the lease gate.
        sp::AuthorityGrant foreign = grant; foreign.owner += 1;
        VERIFY(error_is(fanout.step(*authority.ptr(), gate(), foreign, frame_ms), sp::Error::PermissionDenied));
        return true;
    }
    bool make_catalog() {
        auto *node = memnew(SuperposRecoveryFixtureNode);
        Ref<PackedScene> scene; scene.instantiate(); VERIFY(scene->pack(node) == OK); memdelete(node);
        Ref<SuperposSpawnEntry> entry; entry.instantiate(); entry->set_schema_id(19); entry->set_resource_id(101); entry->set_scene(scene);
        Dictionary mappings;
        for (int64_t id = 1; id <= 2; ++id) { Dictionary mapping; mapping["node"] = NodePath(); mapping["property"] = id == 1 ? "value" : "second"; mappings[id] = mapping; }
        entry->set_fields(mappings); catalog.instantiate(); TypedArray<SuperposSpawnEntry> entries; entries.append(entry); catalog->set_entries(entries);
        return true;
    }
    bool start_client(unsigned i) {
        auto *spawner = memnew(SuperposSpawner); spawner->set_session(clients[i]); spawner->set_catalog(catalog);
        SceneTree::get_singleton()->get_root()->add_child(spawner); spawners[i] = spawner;
        Dictionary config; config["replica_epoch"] = 9; config["maximum_active"] = 8; config["maximum_transitions"] = 8; config["native_bytes"] = 1 << 20;
        VERIFY(spawner->start(config) == OK); spawner->set_process(false);
        return true;
    }
    // Every restored entity is a ready replica with exactly the sealed values.
    bool converged(unsigned i) {
        auto *spawner = spawners[i]; if (!spawner) return false;
        TypedArray<SuperposReplicaView> views = spawner->get_replicas();
        if (views.size() != int(expected_objects) || spawner->get_child_count() != int(expected_objects)) return false;
        std::array<bool, 8> matched{};
        bool complete = true;
        auto checked = for_each_entity(std::span<const std::byte>(final_payload.data(), final_bytes), [&](const WorldEntity &entity) -> sp::Status {
            const int value = int(detail::u32(entity.state.data())), second = int(detail::u32(entity.state.data() + 4));
            bool found = false;
            for (int c = 0; c < spawner->get_child_count() && !found; ++c) {
                Node *child = spawner->get_child(c);
                if (!matched[c] && int(child->get("value")) == value && int(child->get("second")) == second) { matched[c] = true; found = true; }
            }
            complete = complete && found;
            return {};
        });
        if (!checked || !complete) return false;
        for (int v = 0; v < views.size(); ++v) {
            Ref<SuperposReplicaView> view = views[v];
            Dictionary status = view->read_status(); Dictionary scene = status["scene"];
            if (int(status["error"]) != OK || !bool(status["canonical_ready"]) || String(scene["scene_phase"]) != String("ready")) return false;
        }
        return true;
    }
    // Lease lapse: minting and restore time out, and the fan-out closes every
    // link's egress, so nothing more is sent or retransmitted.
    bool expiry() {
        const auto expires = capability->expires_us();
        observe_to(expires);
        VERIFY(error_is(coordinator->restore_read(verified, 16), sp::Error::Timeout));
        std::optional<WorldParticipant> participant; VERIFY(participant_for(*clients[0].ptr(), participant));
        auto made = restore_for(*capability, *participant); VERIFY(made);
        VERIFY(error_is((*made)->start(), sp::Error::Timeout) && !participant->active());
        VERIFY(error_is(fanout.step(*authority.ptr(), gate(), grant, frame_ms), sp::Error::Timeout));
        std::array<std::uint64_t, clients_count> sent{};
        for (unsigned i = 0; i < clients_count; ++i) { auto link = fanout.status(i); VERIFY(link && !link->egress); sent[i] = link->totals.sent; }
        for (unsigned n = 0; n < 4; ++n) for (auto &session : links) VERIFY(session->advance_tick() == OK);
        for (unsigned i = 0; i < clients_count; ++i) { auto link = fanout.status(i); VERIFY(link && link->totals.sent == sent[i] && link->totals.gated >= 4); }
        VERIFY(authority_live().value_or(0) == expected_objects);
        return true;
    }
    bool cleanup() {
        for (auto *&spawner : spawners) if (spawner) { (void)spawner->stop(); memdelete(spawner); spawner = nullptr; }
        for (auto &session : links) if (session.is_valid()) { (void)SuperposRecoveryAccess::detach_authority(*session.ptr()); (void)session->close_checked(); }
        for (auto &session : clients) if (session.is_valid()) (void)session->close_checked();
        for (auto *session : {&authority, &producer}) if (session->is_valid()) (void)(*session)->close_checked();
        SuperposNativeReceiverAccess::drain();
        for (auto &session : links) session.unref();
        for (auto &session : clients) session.unref();
        authority.unref(); producer.unref(); catalog.unref();
        SuperposNativeReceiverAccess::drain();
        coordinator.reset(); bootstrap.reset();
        if (worker) { VERIFY(worker->request_stop()); VERIFY(until([&] { return bool(worker->poll_join(10)); })); VERIFY(worker->failure() == sp::Error::None); worker.reset(); }
        lease.reset();
        path(filename, "superpos-recovery-fixture.sqlite");
        return true;
    }
    void report() {
        for (unsigned i = 0; i < clients_count; ++i) {
            auto link = fanout.status(i);
            std::printf("RECOVERY_LINK index=%u ok=%d error=%u attached=%d egress=%d entities=%u queued=%u ready=%u sent=%llu applied=%llu backpressured=%llu dropped=%u\n",
                i, int(bool(link)), link ? 0u : unsigned(link.error()), link ? int(link->attached) : 0, link ? int(link->egress) : 0,
                link ? link->entities : 0u, link ? link->queued : 0u, link ? link->ready : 0u,
                (unsigned long long)(link ? link->totals.sent : 0), (unsigned long long)(link ? link->totals.transport_applied : 0),
                (unsigned long long)(link ? link->totals.backpressured : 0), wires[i][0].dropped + wires[i][1].dropped);
        }
        std::fflush(stdout);
    }
    bool advance_all() {
        for (unsigned i = 0; i < clients_count; ++i) if (bound[i]) for (unsigned side = 0; side < 2; ++side) {
            auto &session = side ? clients[i] : links[i];
            const Error advanced = session->advance_tick();
            if (advanced != OK && advanced != ERR_BUSY) {
                std::printf("RECOVERY_PUMP link=%u side=%s error=%d core=%u state=%s stage=%u frames=%u\n", i, side ? "client" : "link",
                    int(advanced), unsigned(SuperposRecoveryAccess::network_failure(*session.ptr())), session->get_state().utf8().get_data(), stage, frames);
                report();
            }
            VERIFY(advanced == OK || advanced == ERR_BUSY);
        }
        return true;
    }
    bool step() {
        if (failed) return false;
        if (std::chrono::steady_clock::now() >= deadline) report();
        VERIFY(std::chrono::steady_clock::now() < deadline);
        frame_ms += 16;
        if (!stage) {
            VERIFY(crypto.initialize());
            auto made = sp::PsaSha256::create(crypto); VERIFY(made); digest.emplace(std::move(*made));
            auto other = sp::PsaSha256::create(crypto); VERIFY(other); worker_digest.emplace(std::move(*other));
            auto random = sp::PsaLeaseNonce::create(crypto); VERIFY(random); nonce.emplace(std::move(*random));
            VERIFY(produce()); VERIFY(takeover()); VERIFY(sessions()); VERIFY(make_catalog());
            stage = 1;
        }
        if (stage >= 2) {
            auto progress = fanout.step(*authority.ptr(), gate(), grant, frame_ms); VERIFY(progress);
        }
        VERIFY(advance_all());
        if (stage == 1) {
            for (unsigned i = 0; i < clients_count; ++i) if (i != late && !ready(i)) return false;
            // Loss starts only after admission; delivery must retransmit.
            wires[lossy][0].armed = wires[lossy][1].armed = true;
            VERIFY(restore());
            for (unsigned i = 0; i < clients_count; ++i) if (i != late) VERIFY(start_client(i));
            stage = 2; return false;
        }
        for (unsigned i = 0; i < clients_count; ++i) if (spawners[i]) (void)spawners[i]->project_pending();
        if (stage == 2) {
            ++frames;
            for (unsigned i = 0; i < clients_count; ++i) if (i != late && !converged_at[i]) converged_at[i] = converged(i);
            for (unsigned i = 0; i < clients_count; ++i) if (i != late && !converged_at[i]) return false;
            // The late client is admitted only now; its link attaches on admission.
            auto waiting = fanout.status(late); VERIFY(waiting && !waiting->attached);
            VERIFY(bind_pair(late)); stage = 3; return false;
        }
        if (stage == 3) {
            ++frames;
            if (!ready(late)) return false;
            if (!spawners[late]) VERIFY(start_client(late));
            if (!converged(late)) return false;
            // Sender-side readiness follows the clients' SpawnApplied receipts,
            // which the bridge consumes on a later pump than client projection.
            for (unsigned i = 0; i < clients_count; ++i) {
                auto link = fanout.status(i); VERIFY(link);
                if (!link->attached || link->ready != expected_objects) return false;
            }
            for (unsigned i = 0; i < clients_count; ++i) {
                auto link = fanout.status(i);
                VERIFY(link && link->attached && link->egress && link->entities == expected_objects && link->ready == expected_objects);
                VERIFY(link->queued == 2 * expected_objects && link->totals.sent > 0 && link->totals.pumps > 0);
                VERIFY(converged(i));
            }
            VERIFY(wires[lossy][0].dropped + wires[lossy][1].dropped > 0);
            dropped = wires[lossy][0].dropped + wires[lossy][1].dropped;
            VERIFY(expiry()); VERIFY(cleanup());
            return true;
        }
        return false;
    }
};
Run *run{};
bool finished{};
void idle() {
    if (finished) return;
    if (!run) run = memnew(Run);
    if (run->step()) {
        finished = true;
        const unsigned frames = run->frames, dropped = run->dropped;
        memdelete(run); run = nullptr;
        std::printf("EGP_RECOVERY_RESTORE_OK checks=%u cases=8 sealed_restore=1 apply=1 lease_restored=1 publication=replica_authority_session "
            "clients=4 client_converged=1 late_join=1 lossy_link=1 dropped_packets=%u publish_before_restore=refused abort_untouched=1 "
            "superseded_capability=stale expired_capability=timeout egress_closed=1 convergence_frames=%u\n", checks, dropped, frames);
        std::fflush(stdout); SceneTree::get_singleton()->quit(0);
    }
}
}
void superpos_register_recovery_fixture() {
    for (const String &arg : OS::get_singleton()->get_cmdline_user_args()) if (arg == "--superpos-recovery-fixture") {
        GDREGISTER_CLASS(superpos_egp::recovery::fixture::SuperposRecoveryFixtureNode);
        SceneTree::add_idle_callback(superpos_egp::recovery::fixture::idle); return;
    }
}
