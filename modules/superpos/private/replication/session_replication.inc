// SPDX-License-Identifier: MIT
// Included by superpos_session.cpp after SuperposSession::Impl and
// EngineNetwork are complete. Owner/main thread only; no user callbacks run
// between the World read and the link queues, so borrowed canonical spans
// remain valid for the whole publish barrier.
#include "superpos_replication_server.h"
struct SuperposReplicationServer::Relevance {
    superpos_egp::ChargedArray<superpos_egp::replication::SlotAttributes> attributes;
    superpos_egp::ChargedArray<superpos::RegionEntry> entries;
    superpos_egp::ChargedArray<superpos::RegionBucket> buckets;
    std::optional<superpos::RegionIndex> index;
    uint64_t region_overflow = 0;
    explicit Relevance(superpos::Allocator &allocator) noexcept
        : attributes(allocator, superpos::MemoryDomain::Replication), entries(allocator, superpos::MemoryDomain::Replication),
          buckets(allocator, superpos::MemoryDomain::Replication) {}
};
namespace {
uint64_t next_link_incarnation = 0;
}
bool SuperposReplicationAccess::idle(const SuperposSession &session) {
    return !session.owner_retired && !session.closing && !session.managed_reload_paused && !session.simulation_in_flight &&
        !session.callbacks_in_flight && !session.pending_native_retirement;
}
namespace {
bool unsigned_value(const Dictionary &configuration, const char *key, uint64_t fallback, uint64_t &r_value) {
    if (!configuration.has(key)) { r_value = fallback; return true; }
    const Variant value = configuration[key];
    if (value.get_type() != Variant::INT) { return false; }
    r_value = uint64_t(int64_t(value));
    return true;
}
bool keys_allowed(const Dictionary &configuration, std::initializer_list<const char *> allowed) {
    for (int i = 0; i < configuration.size(); ++i) {
        const Variant key = configuration.get_key_at_index(i);
        if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) { return false; }
        const String name = key;
        bool found = false;
        for (const char *candidate : allowed) { found = found || name == candidate; }
        if (!found) { return false; }
    }
    return true;
}
}

Error SuperposReplicationAccess::configure(SuperposReplicationServer &server, SuperposSession &world, const Dictionary &configuration) {
    if (!Thread::is_main_thread() || Thread::get_caller_id() != server.owner_thread || Thread::get_caller_id() != world.owner_thread) { return ERR_BUSY; }
    if (server.publishing) { return ERR_BUSY; }
    if (server.world.is_valid() || server.peer_count) { return ERR_ALREADY_IN_USE; }
    if (!SuperposReplicationAccess::idle(world)) { return world.owner_retired ? ERR_UNCONFIGURED : ERR_BUSY; }
    if (!world.impl || !world.impl->world || !world.impl->slots) { return ERR_UNCONFIGURED; }
    if (!keys_allowed(configuration, {"maximum_peers", "maximum_regions", "maximum_per_region"})) { return ERR_INVALID_PARAMETER; }
    uint64_t limit = 0, regions = 0, members = 0;
    if (!unsigned_value(configuration, "maximum_peers", 32, limit) || !limit || limit > SuperposReplicationServer::maximum_peers ||
            !unsigned_value(configuration, "maximum_regions", 256, regions) || !regions || regions > 65536 ||
            !unsigned_value(configuration, "maximum_per_region", 1000, members) || !members || members > 4096) { return ERR_INVALID_PARAMETER; }
    auto identity = world.impl->world->world().identity();
    if (!identity) { return translate(identity.error()); }
    // Complete relevance capacity before the server can attach any link.
    auto created = superpos_egp::captured_create<SuperposReplicationServer::Relevance>(superpos_egp::module_backing(), superpos::MemoryDomain::Replication, superpos_egp::module_backing());
    if (!created) { return translate(created.error()); }
    auto &relevance = **created;
    for (auto status : {relevance.attributes.initialize(world.impl->capacity), relevance.entries.initialize(world.impl->capacity),
            relevance.buckets.initialize(size_t(regions))}) {
        if (!status) { return translate(status.error()); }
    }
    auto index = superpos::RegionIndex::create(relevance.entries.span(), relevance.buckets.span(), uint32_t(members));
    if (!index) { return translate(index.error()); }
    relevance.index.emplace(std::move(*index));
    SuperposReplicationAccess::release(server);
    server.relevance = created->release();
    server.region_limit = uint32_t(regions);
    server.region_members = uint32_t(members);
    server.world = world.get_instance_id();
    server.world_instance = identity->instance;
    server.world_epoch = identity->config.authority_epoch;
    server.peer_limit = uint32_t(limit);
    return OK;
}

Error SuperposReplicationAccess::attach(SuperposReplicationServer &server, SuperposSession &link, const Dictionary &configuration, uint64_t &r_incarnation) {
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!Thread::is_main_thread() || Thread::get_caller_id() != server.owner_thread || Thread::get_caller_id() != link.owner_thread) { return ERR_BUSY; }
    if (server.publishing) { return ERR_BUSY; }
    Ref<SuperposSession> world(Object::cast_to<SuperposSession>(ObjectDB::get_instance(server.world)));
    if (world.is_null()) { return ERR_UNCONFIGURED; }
    if (world.ptr() == &link) { return ERR_INVALID_PARAMETER; }
    if (!SuperposReplicationAccess::idle(link) || !SuperposReplicationAccess::idle(*world.ptr())) { return link.owner_retired ? ERR_UNCONFIGURED : ERR_BUSY; }
    if (!world->impl || !world->impl->world || !link.impl || !link.impl->world || !link.impl->registry || !link.impl->network) { return ERR_UNCONFIGURED; }
    auto *network = link.impl->network.get();
    if (network->error != OK) { return network->error; }
    if (network->receiver || network->publisher) { return ERR_ALREADY_IN_USE; }
    // The bridge keeps its Session pointer between calls. Only a link that
    // owns a stable in-place Session (native UDP/DTLS) qualifies; paired RTC
    // Sessions are borrowed per lease and are not yet supported here.
    if (!network->session) { return ERR_UNAVAILABLE; }
#if defined(SUPERPOS_HAS_DURABLE_RECOVERY)
    if (network->authority) { return ERR_ALREADY_IN_USE; }
#endif
    if (server.peer_count >= server.peer_limit) { return ERR_OUT_OF_MEMORY; }
    for (uint32_t i = 0; i < server.peer_count; ++i) { if (server.peers[i].link == link.get_instance_id()) { return ERR_ALREADY_IN_USE; } }
    auto world_identity = world->impl->world->world().identity();
    if (!world_identity) { return translate(world_identity.error()); }
    if (world_identity->instance != server.world_instance || world_identity->config.authority_epoch != server.world_epoch) { return ERR_INVALID_DATA; }
    // The link carries exactly the World's frozen schema registry and epoch.
    if (link.impl->world->fingerprint() != world->impl->world->fingerprint()) { return ERR_INVALID_DATA; }
    auto link_epoch = link.impl->world->world().authority_epoch();
    if (!link_epoch) { return translate(link_epoch.error()); }
    if (*link_epoch != server.world_epoch) { return ERR_INVALID_DATA; }
    if (!keys_allowed(configuration, {"peer", "replica_epoch", "maximum_active", "maximum_transitions", "scan_budget",
            "control_channel", "state_channel", "bulk_channel", "rpc_channel", "state_budget", "baseline_promotion"})) { return ERR_INVALID_PARAMETER; }
    uint64_t state_budget = 0, promotion = 1;
    if (!unsigned_value(configuration, "state_budget", 0, state_budget) || state_budget > (1u << 20) ||
            !unsigned_value(configuration, "baseline_promotion", 1, promotion) || promotion > 1) { return ERR_INVALID_PARAMETER; }
    if (!server.relevance) { return ERR_UNCONFIGURED; }
    uint64_t peer = 0, replica = 0, active = 0, transitions = 0, budget = 0, control = 0, state = 0, bulk = 0, rpc_channel = 0;
    const bool has_peer = configuration.has("peer");
    if (!unsigned_value(configuration, "peer", 0, peer) || !unsigned_value(configuration, "replica_epoch", 1, replica) ||
            !unsigned_value(configuration, "maximum_active", 1000, active) || !unsigned_value(configuration, "maximum_transitions", 64, transitions) ||
            !unsigned_value(configuration, "scan_budget", 4096, budget) || !unsigned_value(configuration, "control_channel", 0, control) ||
            !unsigned_value(configuration, "state_channel", 1, state) || !unsigned_value(configuration, "bulk_channel", 2, bulk) ||
            !unsigned_value(configuration, "rpc_channel", 3, rpc_channel)) { return ERR_INVALID_PARAMETER; }
    if (rpc_channel >= 32 || rpc_channel == control || rpc_channel == state || rpc_channel == bulk) { return ERR_INVALID_PARAMETER; }
    if (!replica || !active || active > 1000 || !transitions || transitions > 64 || !budget || budget > 100000 ||
            control >= 32 || state >= 32 || bulk >= 32 || control == state || control == bulk || state == bulk) { return ERR_INVALID_PARAMETER; }
    size_t stride = 0;
    for (const auto &schema : link.impl->schemas.span()) { stride = std::max(stride, schema.state_bytes()); }
    superpos_egp::replication::LinkConfig config;
    config.context.replica = replica;
    config.peer = peer;
    config.maximum_active = uint32_t(active);
    config.maximum_transitions = uint32_t(transitions);
    config.scan_budget = uint32_t(budget);
    config.routes = {uint8_t(control), uint8_t(state), uint8_t(bulk)};
    config.state_budget_bytes = uint32_t(state_budget);
    config.baseline_promotion = promotion == 1;
    auto incarnation = superpos::increment(next_link_incarnation);
    if (!incarnation) { return translate(incarnation.error()); }
    auto created = superpos_egp::captured_create<superpos_egp::replication::LinkAuthority>(link.impl->allocator, superpos::MemoryDomain::Replication, link.impl->allocator);
    if (!created) { return translate(created.error()); }
    auto reserved = (*created)->reserve(config, stride, *incarnation, server.region_members);
    if (!reserved) { return translate(reserved.error()); }
    auto endpoint = superpos_egp::captured_create<superpos_egp::replication::RpcEndpoint>(link.impl->allocator, superpos::MemoryDomain::Session, link.impl->allocator);
    if (!endpoint) { return translate(endpoint.error()); }
    if (auto rpc_reserved = (*endpoint)->reserve(uint8_t(rpc_channel)); !rpc_reserved) { return translate(rpc_reserved.error()); }
    next_link_incarnation = *incarnation;
    network->publisher = std::move(*created);
    network->rpc = std::move(*endpoint);
    network->publisher_peer = has_peer ? std::optional<superpos::PeerId>(peer) : std::nullopt;
    server.peers[server.peer_count++] = {link.get_instance_id(), *incarnation};
    r_incarnation = *incarnation;
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

Error SuperposReplicationAccess::detach(SuperposSession &link, uint64_t incarnation) {
    if (!Thread::is_main_thread() || Thread::get_caller_id() != link.owner_thread) { return ERR_BUSY; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (link.callbacks_in_flight || link.simulation_in_flight) { return ERR_BUSY; }
    if (link.impl && link.impl->network && link.impl->network->publisher && link.impl->network->publisher->incarnation() == incarnation) {
        link.impl->network->publisher.reset();
        link.impl->network->publisher_peer.reset();
        link.impl->network->rpc.reset();
    }
#endif
    return OK;
}

Dictionary SuperposReplicationAccess::status(SuperposSession &link, uint64_t incarnation) {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    if (Thread::get_caller_id() != link.owner_thread) { result["error"] = ERR_BUSY; return result; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!link.impl || !link.impl->network || !link.impl->network->publisher || link.impl->network->publisher->incarnation() != incarnation) { return result; }
    const auto &publisher = *link.impl->network->publisher;
    const auto counts = publisher.counts();
    const auto &totals = publisher.totals();
    const auto &config = publisher.config();
    result["error"] = publisher.failure() == superpos::Error::None ? OK : translate(publisher.failure());
    result["failure"] = int(publisher.failure());
    result["attached"] = true;
    result["bound"] = publisher.bound();
    result["network_error"] = link.impl->network->error;
    // Exact core error behind a translated link failure (bind or step).
    result["network_failure"] = int(link.impl->network->failure);
    if (publisher.bound()) {
        result["peer"] = superpos_egp::signed_bits(config.peer);
        result["authority_epoch"] = superpos_egp::signed_bits(config.context.authority);
        result["connection_epoch"] = superpos_egp::signed_bits(config.context.connection);
    }
    result["replica_epoch"] = superpos_egp::signed_bits(config.context.replica);
    result["tracked"] = counts.tracked;
    result["spawning"] = counts.spawning;
    result["ready"] = counts.ready;
    result["retiring"] = counts.retiring;
    result["resetting"] = counts.resetting;
    result["repairs_pending"] = counts.dirty;
    result["wire_revision"] = superpos_egp::signed_bits(publisher.wire_revision());
    result["interest_filter"] = !publisher.interest_all();
    PackedInt64Array interest;
    for (const auto region : publisher.interest()) { interest.push_back(int64_t(region)); }
    result["interest"] = interest;
    Dictionary counters;
    counters["steps"] = superpos_egp::signed_bits(totals.steps);
    counters["pumps"] = superpos_egp::signed_bits(totals.pumps);
    counters["spawns"] = superpos_egp::signed_bits(totals.spawns);
    counters["leaves"] = superpos_egp::signed_bits(totals.leaves);
    counters["ownership_resets"] = superpos_egp::signed_bits(totals.ownership_resets);
    counters["relevance_leaves"] = superpos_egp::signed_bits(totals.relevance_leaves);
    counters["group_publications"] = superpos_egp::signed_bits(totals.group_publications);
    counters["groups_deferred"] = superpos_egp::signed_bits(totals.groups_deferred);
    counters["promotions"] = superpos_egp::signed_bits(totals.promotions);
    counters["budget_limited"] = superpos_egp::signed_bits(totals.budget_limited);
    counters["publications"] = superpos_egp::signed_bits(totals.publications);
    counters["published_members"] = superpos_egp::signed_bits(totals.published_members);
    counters["repairs"] = superpos_egp::signed_bits(totals.repairs);
    counters["repair_requests"] = superpos_egp::signed_bits(totals.repair_requests);
    counters["deferred"] = superpos_egp::signed_bits(totals.deferred);
    counters["capture_pressure"] = superpos_egp::signed_bits(totals.capture_pressure);
    counters["sent"] = superpos_egp::signed_bits(totals.sent);
    counters["transport_applied"] = superpos_egp::signed_bits(totals.transport_applied);
    counters["received"] = superpos_egp::signed_bits(totals.received);
    counters["discarded"] = superpos_egp::signed_bits(totals.discarded);
    counters["backpressured"] = superpos_egp::signed_bits(totals.backpressured);
    result["totals"] = counters;
    if (link.impl->network->rpc) { result["rpc"] = rpc_totals_dictionary(*link.impl->network->rpc); }
#endif
    return result;
}

void *SuperposReplicationAccess::bound_publisher(SuperposSession &link, uint64_t incarnation) {
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!link.impl || !link.impl->network || !link.impl->network->publisher || !link.impl->network->rpc) { return nullptr; }
    auto *publisher = link.impl->network->publisher.get();
    return publisher->incarnation() == incarnation && publisher->bound() ? publisher : nullptr;
#else
    return nullptr;
#endif
}

Error SuperposReplicationAccess::send_rpc(SuperposReplicationServer &server, SuperposSession &link, uint64_t incarnation,
        uint64_t handle, uint64_t rpc_id, const PackedByteArray &payload) {
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!Thread::is_main_thread() || Thread::get_caller_id() != server.owner_thread || Thread::get_caller_id() != link.owner_thread) { return ERR_BUSY; }
    if (server.publishing || !SuperposReplicationAccess::idle(link)) { return ERR_BUSY; }
    auto *publisher = static_cast<superpos_egp::replication::LinkAuthority *>(bound_publisher(link, incarnation));
    if (!publisher) { return ERR_UNCONFIGURED; }
    if (link.impl->network->error != OK) { return link.impl->network->error; }
    Ref<SuperposSession> world(Object::cast_to<SuperposSession>(ObjectDB::get_instance(server.world)));
    if (world.is_null() || !world->impl || !world->impl->world) { return ERR_UNCONFIGURED; }
    if (!rpc_id) { return ERR_INVALID_PARAMETER; }
    auto entity = world->impl->world->world().view({handle});
    if (!entity) { return translate(entity.error()); }
    const auto *descriptor = entity->schema->rpc(rpc_id);
    if (!descriptor) { return ERR_UNAVAILABLE; }
    if (size_t(payload.size()) > descriptor->maximum_payload) { return ERR_INVALID_PARAMETER; }
    const superpos_egp::replication::RpcFrame frame{rpc_id, entity->handle, entity->ownership_revision, byte_view(payload)};
    auto sent = link.impl->network->rpc->send(*link.impl->network->session, link.impl->tick, frame);
    return sent ? OK : translate(sent.error());
#else
    return ERR_UNAVAILABLE;
#endif
}

Array SuperposReplicationAccess::take_rpcs(SuperposReplicationServer &server, SuperposSession &link, uint64_t incarnation, uint32_t maximum) {
    Array result;
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!Thread::is_main_thread() || Thread::get_caller_id() != server.owner_thread || Thread::get_caller_id() != link.owner_thread) { return result; }
    if (server.publishing) { return result; }
    auto *publisher = static_cast<superpos_egp::replication::LinkAuthority *>(bound_publisher(link, incarnation));
    if (!publisher) { return result; }
    Ref<SuperposSession> world(Object::cast_to<SuperposSession>(ObjectDB::get_instance(server.world)));
    if (world.is_null() || !world->impl || !world->impl->world || !SuperposReplicationAccess::idle(*world.ptr())) { return result; }
    auto &endpoint = *link.impl->network->rpc;
    auto &inbox = endpoint.inbox();
    while (inbox.size() && uint32_t(result.size()) < std::min<uint32_t>(maximum, 64)) {
        const auto frame = inbox.frame(0);
        // Execution-time authorization against the live World: permission,
        // current ownership revision, authority epoch and payload bound.
        superpos::RpcContext context;
        context.sender = publisher->config().peer;
        context.authority_epoch = server.world_epoch;
        context.ownership_revision = frame.ownership_revision;
        context.admitted = true;
        auto authorized = world->impl->world->world().authorize_rpc(frame.handle, frame.rpc, context, frame.payload.size());
        if (!authorized) {
            const auto error = authorized.error();
            if (error == superpos::Error::PermissionDenied) { ++endpoint.totals().rejected_permission; }
            else if (error == superpos::Error::Unsupported) { ++endpoint.totals().rejected_unsupported; }
            else { ++endpoint.totals().rejected_stale; }
            inbox.remove(0);
            continue;
        }
        PackedByteArray payload;
        if (payload.resize(int64_t(frame.payload.size())) != OK) { break; }
        if (!frame.payload.empty()) { memcpy(payload.ptrw(), frame.payload.data(), frame.payload.size()); }
        Dictionary call;
        call["handle"] = superpos_egp::signed_bits(frame.handle.value);
        call["rpc_id"] = superpos_egp::signed_bits(frame.rpc);
        call["sender"] = superpos_egp::signed_bits(context.sender);
        call["ownership_revision"] = superpos_egp::signed_bits(frame.ownership_revision);
        call["payload"] = payload;
        result.append(call);
        ++endpoint.totals().taken;
        inbox.remove(0);
    }
#endif
    return result;
}

Dictionary SuperposReplicationAccess::publish(SuperposReplicationServer &server) {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    if (!Thread::is_main_thread() || Thread::get_caller_id() != server.owner_thread) { result["error"] = ERR_BUSY; return result; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (server.publishing) { result["error"] = ERR_BUSY; return result; }
    Ref<SuperposSession> world(Object::cast_to<SuperposSession>(ObjectDB::get_instance(server.world)));
    if (world.is_null()) { return result; }
    if (Thread::get_caller_id() != world->owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (!SuperposReplicationAccess::idle(*world.ptr())) { result["error"] = world->owner_retired ? ERR_UNCONFIGURED : ERR_BUSY; return result; }
    if (!world->impl || !world->impl->world || !world->impl->slots) { return result; }
    auto identity = world->impl->world->world().identity();
    if (!identity) { result["error"] = translate(identity.error()); return result; }
    // A reconfigured World (new creation identity or epoch) never inherits
    // the previous World's replicas on any link.
    if (identity->instance != server.world_instance || identity->config.authority_epoch != server.world_epoch) { result["error"] = ERR_INVALID_DATA; return result; }
    if (!server.relevance || server.relevance->attributes.size() != size_t(world->impl->capacity)) { result["error"] = ERR_INVALID_DATA; return result; }
    server.publishing = true;
    struct Publishing { bool &flag; ~Publishing() { flag = false; } } publishing{server.publishing};
    // One global capture of World membership per publish: attributes reset on
    // slot reuse and the RegionIndex follows spawns and destroys. Links then
    // enroll from the index instead of scanning the World per peer.
    auto &relevance = *server.relevance;
    for (uint32_t i = 0; i < world->impl->capacity; ++i) {
        const auto &slot = world->impl->slots[i];
        auto &attribute = relevance.attributes[i];
        const superpos::ObjectHandle handle = superpos::ObjectHandle::from_parts(i + 1, slot.generation);
        if (attribute.indexed && (!slot.live || attribute.indexed != handle.value)) {
            (void)relevance.index->remove({attribute.indexed});
            attribute.indexed = 0;
        }
        if (!slot.live) { continue; }
        if (attribute.generation != slot.generation) { attribute = {}; attribute.generation = slot.generation; }
        if (!attribute.indexed) {
            auto inserted = relevance.index->insert(handle, attribute.region);
            if (inserted) { attribute.indexed = handle.value; }
            else if (relevance.region_overflow != UINT64_MAX) { ++relevance.region_overflow; }
        }
    }
    const uint64_t step = server.publishes == UINT64_MAX ? UINT64_MAX : server.publishes + 1;
    const superpos_egp::replication::WorldFrame frame{&world->impl->world->world(),
        {world->impl->slots, size_t(world->impl->capacity)}, relevance.attributes.span(), &*relevance.index,
        world->impl->tick, OS::get_singleton()->get_ticks_msec(), step};
    uint32_t stepped = 0, waiting = 0, busy = 0, failed = 0, dropped = 0;
    for (uint32_t i = 0; i < server.peer_count;) {
        const auto &peer = server.peers[i];
        Ref<SuperposSession> link(Object::cast_to<SuperposSession>(ObjectDB::get_instance(peer.link)));
        auto *network = link.is_valid() && link->impl ? link->impl->network.get() : nullptr;
        auto *publisher = network ? network->publisher.get() : nullptr;
        if (!publisher || publisher->incarnation() != peer.incarnation || Thread::get_caller_id() != link->owner_thread) {
            server._remove_peer(i); ++dropped; continue;
        }
        ++i;
        if (!SuperposReplicationAccess::idle(*link.ptr())) { ++busy; continue; }
        if (network->error != OK || publisher->failure() != superpos::Error::None) { ++failed; continue; }
        if (!publisher->bound()) {
            if (!network->session || !network->session->ready()) { ++waiting; continue; }
            auto admitted = network->session->identity();
            if (!admitted) { ++waiting; continue; }
            const auto peer_id = network->publisher_peer ? *network->publisher_peer : admitted->remote_peer;
            auto bound = publisher->bind(*network->session, *link->impl->registry, link->impl->schemas.span(),
                server.world_epoch, admitted->connection_epoch, peer_id);
            if (!bound) { network->failure = bound.error(); network->error = translate(bound.error()); ++failed; continue; }
        }
        auto stepped_status = publisher->step(frame);
        if (!stepped_status) { network->failure = stepped_status.error(); network->error = translate(stepped_status.error()); ++failed; continue; }
        ++stepped;
    }
    if (server.publishes != UINT64_MAX) { ++server.publishes; }
    result["error"] = OK;
    result["peers"] = server.peer_count;
    result["stepped"] = stepped;
    result["waiting"] = waiting;
    result["busy"] = busy;
    result["failed"] = failed;
    result["dropped"] = dropped;
    result["tick"] = superpos_egp::signed_bits(frame.tick);
    result["region_overflow"] = superpos_egp::signed_bits(relevance.region_overflow);
#else
    result["error"] = ERR_UNAVAILABLE;
#endif
    return result;
}

void SuperposReplicationAccess::release(SuperposReplicationServer &server) noexcept {
    if (server.relevance) {
        auto *value = server.relevance;
        server.relevance = nullptr;
        std::destroy_at(value);
        superpos_egp::module_backing().deallocate(value);
    }
}

Error SuperposReplicationAccess::set_policy(SuperposReplicationServer &server, uint64_t handle, Policy policy, uint64_t value) {
    if (!Thread::is_main_thread() || Thread::get_caller_id() != server.owner_thread) { return ERR_BUSY; }
    if (server.publishing) { return ERR_BUSY; }
    Ref<SuperposSession> world(Object::cast_to<SuperposSession>(ObjectDB::get_instance(server.world)));
    if (world.is_null() || Thread::get_caller_id() != world->owner_thread) { return ERR_UNCONFIGURED; }
    if (!SuperposReplicationAccess::idle(*world.ptr())) { return ERR_BUSY; }
    // Resolve a live World object and refresh its attributes for the current
    // slot generation (attributes of a reused slot never carry over).
    if (!server.relevance || !world->impl || !world->impl->world || !world->impl->slots) { return ERR_UNCONFIGURED; }
    auto entity = world->impl->world->world().view({handle});
    if (!entity) { return translate(entity.error()); }
    const superpos::Schema *schema = entity->schema;
    const uint32_t slot_index = entity->handle.slot() - 1;
    if (slot_index >= server.relevance->attributes.size()) { return ERR_INVALID_DATA; }
    auto *attribute = &server.relevance->attributes[slot_index];
    if (attribute->generation != entity->handle.generation()) {
        if (attribute->indexed) { (void)server.relevance->index->remove({attribute->indexed}); }
        *attribute = {};
        attribute->generation = entity->handle.generation();
    }
    switch (policy) {
    case Policy::Region: {
        if (value >= server.region_limit) { return ERR_PARAMETER_RANGE_ERROR; }
        const superpos::ObjectHandle object{handle};
        if (attribute->indexed == handle) {
            auto moved = server.relevance->index->move(object, uint32_t(value));
            if (!moved) { return moved.error() == superpos::Error::CapacityExceeded ? ERR_OUT_OF_MEMORY : translate(moved.error()); }
        } else {
            auto inserted = server.relevance->index->insert(object, uint32_t(value));
            if (!inserted) { return inserted.error() == superpos::Error::CapacityExceeded ? ERR_OUT_OF_MEMORY : translate(inserted.error()); }
            attribute->indexed = handle;
        }
        attribute->region = uint32_t(value);
        return OK;
    }
    case Policy::Dormant:
        attribute->dormant = value != 0;
        return OK;
    case Policy::Weight:
        if (!value || value > 256) { return ERR_PARAMETER_RANGE_ERROR; }
        attribute->weight = uint16_t(value);
        return OK;
    case Policy::Group: {
        if (value > UINT32_MAX) { return ERR_PARAMETER_RANGE_ERROR; }
        if (value && value != attribute->group) {
            // Explicit groups: at most 16 live members and 64 KiB of state.
            size_t members = 1, bytes = schema->state_bytes();
            const auto &world_view = world->impl->world->world();
            for (uint32_t i = 0; i < server.relevance->attributes.size(); ++i) {
                const auto &other = server.relevance->attributes[i];
                const auto &slot = world->impl->slots[i];
                if (!slot.live || other.generation != slot.generation || other.group != uint32_t(value)) { continue; }
                auto member = world_view.view(superpos::ObjectHandle::from_parts(i + 1, slot.generation));
                if (!member) { continue; }
                ++members;
                bytes += member->schema->state_bytes();
            }
            if (members > superpos::World::maximum_group_objects || bytes > superpos::World::maximum_group_bytes) { return ERR_OUT_OF_MEMORY; }
        }
        attribute->group = uint32_t(value);
        return OK;
    }
    }
    return ERR_BUG;
}

Dictionary SuperposReplicationAccess::read_policy(SuperposReplicationServer &server, uint64_t handle) {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    Ref<SuperposSession> world(Object::cast_to<SuperposSession>(ObjectDB::get_instance(server.world)));
    if (world.is_null() || Thread::get_caller_id() != world->owner_thread || server.publishing) { return result; }
    if (!world->impl || !world->impl->world || !server.relevance) { return result; }
    auto entity = world->impl->world->world().view({handle});
    if (!entity) { result["error"] = translate(entity.error()); return result; }
    const uint32_t index = entity->handle.slot() - 1;
    superpos_egp::replication::SlotAttributes attribute;
    if (index < server.relevance->attributes.size() && server.relevance->attributes[index].generation == entity->handle.generation()) {
        attribute = server.relevance->attributes[index];
    }
    result["error"] = OK;
    result["region"] = int64_t(attribute.region);
    result["dormant"] = attribute.dormant;
    result["weight"] = int64_t(std::max<uint16_t>(attribute.weight, 1));
    result["group"] = int64_t(attribute.group);
    result["indexed"] = attribute.indexed == handle;
    return result;
}

Error SuperposReplicationAccess::set_interest(SuperposReplicationServer &server, SuperposSession &link, uint64_t incarnation,
        const PackedInt64Array &regions, bool filter) {
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!Thread::is_main_thread() || Thread::get_caller_id() != server.owner_thread || Thread::get_caller_id() != link.owner_thread) { return ERR_BUSY; }
    if (server.publishing) { return ERR_BUSY; }
    if (!link.impl || !link.impl->network || !link.impl->network->publisher || link.impl->network->publisher->incarnation() != incarnation) { return ERR_UNCONFIGURED; }
    if (regions.size() > int(superpos_egp::replication::LinkAuthority::maximum_interest)) { return ERR_OUT_OF_MEMORY; }
    std::array<uint32_t, superpos_egp::replication::LinkAuthority::maximum_interest> values{};
    for (int i = 0; i < regions.size(); ++i) {
        if (regions[i] <= 0 || uint64_t(regions[i]) >= server.region_limit) { return ERR_PARAMETER_RANGE_ERROR; }
        values[size_t(i)] = uint32_t(regions[i]);
    }
    auto applied = link.impl->network->publisher->set_interest(std::span(values).first(size_t(regions.size())), !filter);
    return applied ? OK : (applied.error() == superpos::Error::InvalidArgument ? ERR_INVALID_PARAMETER : translate(applied.error()));
#else
    return ERR_UNAVAILABLE;
#endif
}
