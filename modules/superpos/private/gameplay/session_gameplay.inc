// SPDX-License-Identifier: MIT
// Private gameplay services included by superpos_session.cpp after
// session_fields.inc and the receiver bridge: canonical images for histories,
// predicted spawns (core PredictedSpawns) and SuperposStateHistory (core
// SnapshotHistory with a schema-derived interpolation adapter).
#include "superpos_state_history.h"
#include <cmath>
#include <limits>

Error SuperposReceiverPublicAccess::world_image(SuperposSession &owner, uint64_t handle, Image &r_image, std::span<std::byte> r_canonical) {
    if (Thread::get_caller_id() != owner.owner_thread) { return ERR_BUSY; }
    if (owner.owner_retired || !owner.impl || !owner.impl->world) { return ERR_UNCONFIGURED; }
    if (owner.managed_reload_paused || owner.closing || owner.simulation_in_flight) { return ERR_BUSY; }
    const auto entity = owner.impl->world->world().view({handle});
    if (!entity) { return translate(entity.error()); }
    if (!r_canonical.empty()) {
        if (entity->canonical.size() != r_canonical.size()) { return ERR_INVALID_DATA; }
        memcpy(r_canonical.data(), entity->canonical.data(), r_canonical.size());
    }
    Image image;
    image.session = owner.get_instance_id();
    image.handle = entity->handle.value;
    image.schema = entity->schema->id();
    image.revision = entity->revision;
    image.tick = entity->tick;
    image.owner = entity->owner;
    image.ownership_revision = entity->ownership_revision;
    image.binding = owner.impl->binding_generation;
    r_image = image;
    return OK;
}

Error SuperposReceiverPublicAccess::schema_fields(SuperposSession &owner, uint64_t schema, std::span<superpos::FieldDescriptor> r_fields,
        size_t &r_count, size_t &r_state_bytes) {
    if (Thread::get_caller_id() != owner.owner_thread) { return ERR_BUSY; }
    if (owner.owner_retired || !owner.impl || !owner.impl->world) { return ERR_UNCONFIGURED; }
    if (owner.managed_reload_paused || owner.closing || owner.simulation_in_flight) { return ERR_BUSY; }
    for (const auto &record : owner.impl->records.span()) {
        if (!record.occupied || record.schema.id() != schema) { continue; }
        const auto fields = record.schema.fields();
        if (fields.size() > r_fields.size()) { return ERR_INVALID_DATA; }
        std::copy(fields.begin(), fields.end(), r_fields.begin());
        r_count = fields.size();
        r_state_bytes = record.schema.state_bytes();
        return OK;
    }
    return ERR_DOES_NOT_EXIST;
}

#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
Error SuperposReceiverPublicAccess::replica_image(const SuperposReplicaView &view, Image &r_image, std::span<std::byte> r_canonical) {
    if (!Thread::is_main_thread()) { return ERR_BUSY; }
    Ref<SuperposSession> pin(Object::cast_to<SuperposSession>(ObjectDB::get_instance(view.session_id)));
    if (pin.is_null()) { return ERR_UNCONFIGURED; }
    SuperposSession &owner = *pin.ptr();
    if (Thread::get_caller_id() != owner.owner_thread) { return ERR_BUSY; }
    if (owner.owner_retired || !owner.impl || !owner.impl->network || !owner.impl->network->receiver) { return ERR_UNCONFIGURED; }
    if (owner.managed_reload_paused || owner.closing || owner.simulation_in_flight) { return ERR_BUSY; }
    if (!view.binding || owner.impl->network->spawn_binding != view.binding || view.identity[0] > UINT16_MAX) { return ERR_INVALID_DATA; }
    const superpos::ReplicaKey key{uint16_t(view.identity[0]), view.identity[1], view.identity[2], view.identity[3], view.identity[4], view.identity[5]};
    auto &store = owner.impl->network->receiver->receiver_;
    if (!store) { return ERR_UNCONFIGURED; }
    const auto record = store->inspect_record(key);
    if (!record) { return translate(record.error()); }
    if (record->handle.value != view.handle) { return ERR_INVALID_DATA; }
    if (record->phase != superpos::ReceiverPhase::Ready) { return ERR_BUSY; }
    const auto replica = store->inspect(key);
    if (!replica) { return translate(replica.error()); }
    if (!replica->schema) { return ERR_INVALID_DATA; }
    if (!r_canonical.empty()) {
        if (replica->canonical.size() != r_canonical.size()) { return ERR_INVALID_DATA; }
        memcpy(r_canonical.data(), replica->canonical.data(), r_canonical.size());
    }
    Image image;
    image.session = owner.get_instance_id();
    image.handle = view.handle;
    image.schema = replica->schema->id();
    image.revision = replica->revision;
    image.tick = replica->tick;
    image.owner = replica->owner;
    image.ownership_revision = replica->ownership_revision;
    image.binding = view.binding;
    image.identity = view.identity;
    r_image = image;
    return OK;
}
#else
Error SuperposReceiverPublicAccess::replica_image(const SuperposReplicaView &, Image &, std::span<std::byte>) { return ERR_UNAVAILABLE; }
#endif

// ---------------------------------------------------------------- predicted spawns

Error SuperposSession::_predicted_preflight() const {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired || !impl || !impl->world) { return ERR_UNCONFIGURED; }
    if (managed_reload_paused || closing || simulation_in_flight) { return ERR_BUSY; }
    return impl->predicted ? OK : ERR_UNCONFIGURED;
}

Error SuperposSession::configure_predicted_spawns(uint32_t p_capacity) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired || !impl || !impl->world) { return ERR_UNCONFIGURED; }
    if (managed_reload_paused || closing || simulation_in_flight || callbacks_in_flight) { return ERR_BUSY; }
    if (!p_capacity || p_capacity > 256) { return ERR_INVALID_PARAMETER; }
    if (impl->predicted) { return ERR_ALREADY_IN_USE; }
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (!impl->network) { return ERR_UNCONFIGURED; }
    auto access = impl->network->access();
    if (!access) { return translate(access.error()); }
    auto identity = (*access)->identity();
    if (!identity) { return translate(identity.error()); }
    auto epoch = impl->world->world().authority_epoch();
    if (!epoch) { return translate(epoch.error()); }
    if (!identity->local_peer || !*epoch) { return ERR_UNCONFIGURED; }
    if (auto reserved = impl->predicted_records.initialize(p_capacity); !reserved) { return translate(reserved.error()); }
    if (auto reserved = impl->predicted_keys.initialize(p_capacity); !reserved) {
        impl->predicted_records.clear();
        return translate(reserved.error());
    }
    auto created = superpos::PredictedSpawns::create(*epoch, identity->local_peer, impl->predicted_records.span());
    if (!created) {
        impl->predicted_keys.clear();
        impl->predicted_records.clear();
        return translate(created.error());
    }
    impl->predicted.emplace(std::move(*created));
    impl->predicted_epoch = *epoch;
    impl->predicted_peer = identity->local_peer;
    return OK;
#else
    return ERR_UNAVAILABLE;
#endif
}

Dictionary SuperposSession::begin_predicted_spawn() {
    Dictionary result;
    const Error ready = _predicted_preflight();
    result["error"] = ready;
    if (ready != OK) { return result; }
    auto begun = impl->predicted->begin();
    result["error"] = begun ? OK : translate(begun.error());
    if (!begun) { return result; }
    result["sequence"] = superpos_egp::signed_bits(begun->sequence);
    result["epoch"] = superpos_egp::signed_bits(begun->epoch);
    result["peer"] = superpos_egp::signed_bits(begun->peer);
    return result;
}

Error SuperposSession::confirm_predicted_spawn(uint64_t p_sequence, const Ref<SuperposReplicaView> &p_replica) {
    const Error ready = _predicted_preflight();
    if (ready != OK) { return ready; }
    if (p_replica.is_null() || !p_sequence) { return ERR_INVALID_PARAMETER; }
    SuperposReceiverPublicAccess::Image image;
    const Error read = SuperposReceiverPublicAccess::replica_image(*p_replica.ptr(), image, {});
    if (read != OK) { return read; }
    // The view lookup can run reference callbacks: revalidate this Session.
    const Error still = _predicted_preflight();
    if (still != OK) { return still; }
    if (image.session != get_instance_id()) { return ERR_INVALID_PARAMETER; }
    if (image.owner != impl->predicted_peer) { return ERR_UNAUTHORIZED; }
    const superpos::PredictedSpawnId id{impl->predicted_epoch, impl->predicted_peer, p_sequence};
    auto confirmed = impl->predicted->confirm(id, superpos::ObjectHandle{image.handle}, image.ownership_revision);
    if (!confirmed) { return translate(confirmed.error()); }
    auto records = impl->predicted_records.span();
    for (size_t i = 0; i < records.size(); ++i) {
        if (records[i].phase != superpos::PredictedSpawnPhase::Empty && records[i].id == id) {
            impl->predicted_keys[i] = {image.identity, image.binding};
        }
    }
    return OK;
}

Error SuperposSession::reject_predicted_spawn(uint64_t p_sequence) {
    const Error ready = _predicted_preflight();
    if (ready != OK) { return ready; }
    auto rejected = impl->predicted->reject({impl->predicted_epoch, impl->predicted_peer, p_sequence});
    return rejected ? OK : translate(rejected.error());
}

Error SuperposSession::release_predicted_spawn(uint64_t p_sequence) {
    const Error ready = _predicted_preflight();
    if (ready != OK) { return ready; }
    const superpos::PredictedSpawnId id{impl->predicted_epoch, impl->predicted_peer, p_sequence};
    auto records = impl->predicted_records.span();
    size_t row = records.size();
    for (size_t i = 0; i < records.size(); ++i) {
        if (records[i].phase != superpos::PredictedSpawnPhase::Empty && records[i].id == id) { row = i; }
    }
    auto released = impl->predicted->release(id);
    if (!released) { return translate(released.error()); }
    if (row < records.size()) { impl->predicted_keys[row] = {}; }
    return OK;
}

Dictionary SuperposSession::read_predicted_spawn(uint64_t p_sequence) {
    Dictionary result;
    const Error ready = _predicted_preflight();
    result["error"] = ready;
    if (ready != OK) { return result; }
    const superpos::PredictedSpawnId id{impl->predicted_epoch, impl->predicted_peer, p_sequence};
    auto inspected = impl->predicted->inspect(id);
    if (!inspected) { result["error"] = translate(inspected.error()); return result; }
    auto record = *inspected;
#if defined(SUPERPOS_HAS_DTLS) || defined(SUPERPOS_HAS_RTC)
    if (record.phase == superpos::PredictedSpawnPhase::Confirmed) {
        // A confirmed replica that left this receiver (destroyed, relevance
        // leave, ownership reset or receiver rebind) retires the prediction.
        auto records = impl->predicted_records.span();
        bool present = false;
        for (size_t i = 0; i < records.size(); ++i) {
            if (records[i].phase == superpos::PredictedSpawnPhase::Empty || !(records[i].id == id)) { continue; }
            const auto &key = impl->predicted_keys[i];
            if (impl->network && impl->network->receiver && key.binding && key.binding == impl->network->spawn_binding) {
                // Same handle, incarnation and ownership encoding as confirmed.
                const auto row = impl->network->receiver->find_handle(record.canonical);
                present = row && row->phase != superpos::ReceiverPhase::Empty && row->handle == record.canonical &&
                    row->incarnation == key.identity[1] && row->encoding_epoch == key.identity[5];
            }
        }
        if (!present) {
            auto retired = impl->predicted->canonical_destroyed(record.canonical);
            if (!retired) { result["error"] = translate(retired.error()); return result; }
            record.phase = superpos::PredictedSpawnPhase::Retired;
        }
    }
#endif
    static const char *phases[] = {"empty", "pending", "confirmed", "rejected", "retired"};
    result["phase"] = phases[unsigned(record.phase) < 5 ? unsigned(record.phase) : 0];
    result["sequence"] = superpos_egp::signed_bits(record.id.sequence);
    result["handle"] = superpos_egp::signed_bits(record.canonical.value);
    result["ownership_revision"] = superpos_egp::signed_bits(record.ownership_revision);
    return result;
}

// ---------------------------------------------------------------- state history

namespace {
constexpr size_t history_allocation_slack = 128;

bool interpolated_kind(superpos::FieldKind kind) noexcept {
    return kind == superpos::FieldKind::F32 || kind == superpos::FieldKind::F64 || kind == superpos::FieldKind::QuantizedF32;
}

// Pure schema transform for core SnapshotHistory. Steps every field from the
// earlier sample, then interpolates the selected floating-point fields.
class SchemaHistoryAdapter final : public superpos::SimulationAdapter {
public:
    const superpos::Schema *schema = nullptr;
    uint64_t mask = 0;
    superpos::SimulationDescriptor descriptor() const noexcept override {
        return {schema ? schema->state_bytes() : 0, 0, superpos::SimulationCapability::Interpolation};
    }
    superpos::Status validate_state(std::span<const std::byte> bytes) const noexcept override {
        if (!schema) { return superpos::fail(superpos::Error::NotReady); }
        return schema->validate(bytes);
    }
    superpos::Status validate_input(std::span<const std::byte>) const noexcept override { return superpos::fail(superpos::Error::Unsupported); }
    superpos::Status step(superpos::Tick, std::span<const std::byte>, std::span<const std::byte>, std::span<std::byte>) noexcept override {
        return superpos::fail(superpos::Error::Unsupported);
    }
    superpos::Status interpolate(std::span<const std::byte> a, std::span<const std::byte> b, double alpha,
            std::span<std::byte> output) noexcept override {
        if (!schema || !std::isfinite(alpha) || alpha < 0.0 || alpha > 1.0 || a.size() != schema->state_bytes() ||
                b.size() != a.size() || output.size() != a.size()) {
            return superpos::fail(superpos::Error::InvalidArgument);
        }
        memcpy(output.data(), a.data(), a.size());
        const auto fields = schema->fields();
        for (size_t i = 0; i < fields.size(); ++i) {
            if (!(mask & (uint64_t(1) << i))) { continue; }
            const auto &field = fields[i];
            switch (field.kind) {
                case superpos::FieldKind::F32: {
                    auto x = schema->read(superpos::Field<float>{field.id}, a), y = schema->read(superpos::Field<float>{field.id}, b);
                    if (!x || !y) { return superpos::fail(superpos::Error::InvalidArgument); }
                    const double value = double(*x) + (double(*y) - double(*x)) * alpha;
                    if (auto written = schema->write(superpos::Field<float>{field.id}, output, float(value)); !written) { return written; }
                    break;
                }
                case superpos::FieldKind::F64: {
                    auto x = schema->read(superpos::Field<double>{field.id}, a), y = schema->read(superpos::Field<double>{field.id}, b);
                    if (!x || !y) { return superpos::fail(superpos::Error::InvalidArgument); }
                    if (auto written = schema->write(superpos::Field<double>{field.id}, output, *x + (*y - *x) * alpha); !written) { return written; }
                    break;
                }
                case superpos::FieldKind::QuantizedF32: {
                    auto x = schema->read(superpos::Field<superpos::QuantizedFloat>{field.id}, a);
                    auto y = schema->read(superpos::Field<superpos::QuantizedFloat>{field.id}, b);
                    if (!x || !y) { return superpos::fail(superpos::Error::InvalidArgument); }
                    double value = double(x->value) + (double(y->value) - double(x->value)) * alpha;
                    // Rounding cannot leave the declared range; requantize inside it.
                    value = std::clamp(value, field.minimum, field.maximum);
                    float narrowed = std::clamp(float(value), float(field.minimum), float(field.maximum));
                    if (double(narrowed) < field.minimum) { narrowed = std::nextafter(narrowed, std::numeric_limits<float>::infinity()); }
                    if (double(narrowed) > field.maximum) { narrowed = std::nextafter(narrowed, -std::numeric_limits<float>::infinity()); }
                    if (auto written = schema->write(superpos::Field<superpos::QuantizedFloat>{field.id}, output, superpos::QuantizedFloat{narrowed}); !written) {
                        return written;
                    }
                    break;
                }
                default: break;
            }
        }
        return {};
    }
};

struct HistoryTrack {
    uint64_t handle = 0;
    bool used = false;
    void *block = nullptr;
    std::optional<superpos::SnapshotHistory> history;
    uint64_t *ticks = nullptr;
    uint32_t head = 0, count = 0;
    superpos::Epoch epoch = 1;
    std::array<uint64_t, 6> identity{};
    uint64_t binding = 0;
};

uint64_t history_mix(uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

Error history_error(superpos::Error error, bool query) {
    switch (error) {
        case superpos::Error::None: return OK;
        case superpos::Error::NotReady: return query ? ERR_DOES_NOT_EXIST : ERR_INVALID_DATA;
        case superpos::Error::RecoveryUnavailable: return ERR_DOES_NOT_EXIST;
        case superpos::Error::StaleEpoch: case superpos::Error::ProtocolViolation: return ERR_INVALID_DATA;
        case superpos::Error::InvalidArgument: return ERR_INVALID_PARAMETER;
        default: return translate(error);
    }
}
} // namespace

struct SuperposStateHistory::State {
    superpos::QuotaAllocator quota;
    superpos_egp::ChargedArray<HistoryTrack> tracks{quota, superpos::MemoryDomain::History};
    superpos_egp::ChargedArray<uint32_t> table{quota, superpos::MemoryDomain::History};
    superpos::Buffer staging{quota, superpos::MemoryDomain::History};
    superpos::Buffer output{quota, superpos::MemoryDomain::History};
    std::array<superpos::FieldDescriptor, superpos::Schema::maximum_fields> fields{};
    std::optional<superpos::Schema> schema;
    SchemaHistoryAdapter adapter;
    ObjectID session;
    uint64_t schema_id = 0, binding = 0, maximum_bracket = 30;
    uint32_t capacity = 30, maximum_objects = 64, live = 0;
    size_t state_bytes = 0, block_bytes = 0;
    bool replicas = false;
    int64_t recorded = 0, duplicates = 0, rejected = 0, resets = 0;

    ~State() {
        for (auto &track : tracks.span()) { release(track); }
    }
    void release(HistoryTrack &track) noexcept {
        track.history.reset();
        if (track.block) { quota.deallocate(track.block); }
        track = HistoryTrack{};
    }
    size_t mask() const noexcept { return table.size() - 1; }
    size_t probe(uint64_t handle, bool &r_found) const noexcept {
        size_t position = size_t(history_mix(handle)) & mask();
        while (true) {
            const uint32_t entry = table.span()[position];
            if (!entry) { r_found = false; return position; }
            if (tracks.span()[entry - 1].handle == handle) { r_found = true; return position; }
            position = (position + 1) & mask();
        }
    }
    HistoryTrack *find(uint64_t handle) noexcept {
        bool found = false;
        const size_t position = probe(handle, found);
        return found ? &tracks[table[position] - 1] : nullptr;
    }
    void erase(uint64_t handle) noexcept {
        bool found = false;
        size_t hole = probe(handle, found);
        if (!found) { return; }
        release(tracks[table[hole] - 1]);
        table[hole] = 0;
        --live;
        size_t next = hole;
        while (true) {
            next = (next + 1) & mask();
            const uint32_t entry = table[next];
            if (!entry) { break; }
            const size_t home = size_t(history_mix(tracks[entry - 1].handle)) & mask();
            const bool between = hole <= next ? (hole < home && home <= next) : (hole < home || home <= next);
            if (!between) { table[hole] = entry; table[next] = 0; hole = next; }
        }
    }
    Error create(uint64_t handle, HistoryTrack *&r_track) noexcept {
        if (live >= maximum_objects) { return ERR_PARAMETER_RANGE_ERROR; }
        uint32_t slot = 0;
        while (slot < tracks.size() && tracks[slot].used) { ++slot; }
        if (slot == tracks.size()) { return ERR_PARAMETER_RANGE_ERROR; }
        void *block = quota.allocate(block_bytes, alignof(std::max_align_t), superpos::MemoryDomain::History);
        if (!block) { return ERR_OUT_OF_MEMORY; }
        auto *bytes = static_cast<std::byte *>(block);
        auto *frames = reinterpret_cast<superpos::HistoryFrame *>(bytes);
        const size_t frame_bytes = size_t(capacity) * sizeof(superpos::HistoryFrame);
        const size_t arena_bytes = size_t(capacity) * state_bytes;
        const size_t ticks_offset = (frame_bytes + arena_bytes + state_bytes + 7) & ~size_t(7);
        for (uint32_t i = 0; i < capacity; ++i) { std::construct_at(frames + i); }
        auto created = superpos::SnapshotHistory::create({1, capacity, maximum_bracket}, adapter, std::span(frames, capacity),
            std::span(bytes + frame_bytes, arena_bytes), std::span(bytes + frame_bytes + arena_bytes, state_bytes));
        if (!created) { quota.deallocate(block); return history_error(created.error(), false); }
        HistoryTrack &track = tracks[slot];
        track = HistoryTrack{};
        track.handle = handle;
        track.used = true;
        track.block = block;
        track.history.emplace(std::move(*created));
        track.ticks = reinterpret_cast<uint64_t *>(bytes + ticks_offset);
        bool found = false;
        table[probe(handle, found)] = slot + 1;
        ++live;
        r_track = &track;
        return OK;
    }
    // Records the staged canonical image for one object.
    Error push(uint64_t handle, const std::array<uint64_t, 6> &identity, uint64_t receiver, uint64_t tick, uint64_t revision) noexcept {
        if (!revision) { return ERR_INVALID_DATA; }
        HistoryTrack *track = find(handle);
        const bool fresh = !track;
        if (fresh) {
            const Error created = create(handle, track);
            if (created != OK) { return created; }
            track->identity = identity;
            track->binding = receiver;
        }
        if (track->identity != identity || track->binding != receiver) {
            // A new replica incarnation or ownership encoding: a fresh history.
            if (track->epoch == UINT64_MAX) { return ERR_PARAMETER_RANGE_ERROR; }
            auto cleared = track->history->reset_epoch(track->epoch + 1);
            if (!cleared) { return history_error(cleared.error(), false); }
            ++track->epoch;
            track->head = track->count = 0;
            track->identity = identity;
            track->binding = receiver;
            ++resets;
        }
        const bool duplicate = track->count && track->ticks[(track->head + track->count - 1) % capacity] == tick;
        auto pushed = track->history->push(track->epoch, tick, revision, staging.bytes());
        if (!pushed) {
            ++rejected;
            if (fresh) { erase(handle); }
            return history_error(pushed.error(), false);
        }
        if (duplicate) { ++duplicates; return OK; }
        if (track->count == capacity) { track->head = (track->head + 1) % capacity; --track->count; }
        track->ticks[(track->head + track->count) % capacity] = tick;
        ++track->count;
        ++recorded;
        return OK;
    }
    Dictionary decode(std::span<const std::byte> canonical, uint64_t tick, const PackedInt64Array &requested) const {
        Dictionary result;
        Dictionary values;
        const auto all = schema->fields();
        const int count = requested.is_empty() ? int(all.size()) : requested.size();
        if (count > int(superpos::Schema::maximum_fields)) { result["error"] = ERR_INVALID_PARAMETER; return result; }
        for (int i = 0; i < count; ++i) {
            const uint64_t id = requested.is_empty() ? all[size_t(i)].id : uint64_t(requested[i]);
            const auto *field = schema->field(id);
            const int64_t key = superpos_egp::signed_bits(id);
            if (!field || values.has(key)) { result["error"] = ERR_INVALID_PARAMETER; return result; }
            Variant value;
            const Error decoded = decode_engine_field(*schema, *field, canonical, value);
            if (decoded != OK) { result["error"] = decoded; return result; }
            values[key] = value;
        }
        result["error"] = OK;
        result["values"] = values;
        result["tick"] = superpos_egp::signed_bits(tick);
        return result;
    }
};

SuperposStateHistory::~SuperposStateHistory() {
    if (state) { state->~State(); Memory::free_static(state); state = nullptr; }
}

Error SuperposStateHistory::_preflight() const {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    return state ? OK : ERR_UNCONFIGURED;
}

Error SuperposStateHistory::configure(const Ref<SuperposSession> &p_session, uint64_t p_schema_id, const Dictionary &p_configuration) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (p_session.is_null() || !p_schema_id) { return ERR_INVALID_PARAMETER; }
    static const char *allowed[] = {"capacity", "maximum_objects", "maximum_bracket_ticks", "interpolate_fields"};
    for (int i = 0; i < p_configuration.size(); ++i) {
        const Variant key = p_configuration.get_key_at_index(i);
        if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) { return ERR_INVALID_PARAMETER; }
        bool known = false;
        for (const char *name : allowed) { known = known || String(key) == name; }
        const Variant::Type type = p_configuration.get_value_at_index(i).get_type();
        const bool list = String(key) == "interpolate_fields";
        if (!known || (list ? type != Variant::PACKED_INT64_ARRAY : type != Variant::INT)) { return ERR_INVALID_PARAMETER; }
    }
    const int64_t capacity = p_configuration.has("capacity") ? int64_t(p_configuration["capacity"]) : 30;
    const int64_t objects = p_configuration.has("maximum_objects") ? int64_t(p_configuration["maximum_objects"]) : 64;
    const int64_t bracket = p_configuration.has("maximum_bracket_ticks") ? int64_t(p_configuration["maximum_bracket_ticks"]) : capacity;
    if (capacity < 2 || capacity > 4096 || objects < 1 || objects > 1024 || bracket < 1 || bracket > (int64_t(1) << 53)) { return ERR_INVALID_PARAMETER; }
    std::array<superpos::FieldDescriptor, superpos::Schema::maximum_fields> descriptors{};
    size_t field_count = 0, state_bytes = 0;
    Error error = SuperposReceiverPublicAccess::schema_fields(*p_session.ptr(), p_schema_id, descriptors, field_count, state_bytes);
    if (error != OK) { return error; }
    uint64_t binding = 0;
    if ((error = p_session->query_binding_generation(binding)) != OK) { return error; }
    uint64_t mask = 0;
    if (p_configuration.has("interpolate_fields")) {
        const PackedInt64Array chosen = p_configuration["interpolate_fields"];
        for (int i = 0; i < chosen.size(); ++i) {
            size_t found = field_count;
            for (size_t f = 0; f < field_count; ++f) { if (descriptors[f].id == uint64_t(chosen[i])) { found = f; } }
            if (found == field_count || !interpolated_kind(descriptors[found].kind) || (mask & (uint64_t(1) << found))) { return ERR_INVALID_PARAMETER; }
            mask |= uint64_t(1) << found;
        }
    } else {
        for (size_t f = 0; f < field_count; ++f) { if (interpolated_kind(descriptors[f].kind)) { mask |= uint64_t(1) << f; } }
    }
    const size_t frame_bytes = size_t(capacity) * sizeof(superpos::HistoryFrame);
    const size_t block = ((frame_bytes + size_t(capacity) * state_bytes + state_bytes + 7) & ~size_t(7)) + size_t(capacity) * sizeof(uint64_t);
    size_t table_size = 1;
    while (table_size < size_t(objects) * 2) { table_size <<= 1; }
    const size_t budget = size_t(objects) * (block + history_allocation_slack) + size_t(objects) * sizeof(HistoryTrack) +
        table_size * sizeof(uint32_t) + 2 * state_bytes + 6 * history_allocation_slack;
    if (budget > (size_t(256) << 20)) { return ERR_PARAMETER_RANGE_ERROR; }
    void *memory = Memory::alloc_static(sizeof(State));
    if (!memory) { return ERR_OUT_OF_MEMORY; }
    State *candidate = std::construct_at(static_cast<State *>(memory));
    auto discard = [&]() { candidate->~State(); Memory::free_static(candidate); };
    superpos::MemoryPlan plan;
    plan.limits.fill(0);
    plan.limits[size_t(superpos::MemoryDomain::History)] = budget;
    if (!candidate->quota.bind(superpos_egp::module_backing(), plan) || !candidate->tracks.initialize(size_t(objects)) ||
            !candidate->table.initialize(table_size) || !candidate->staging.resize(state_bytes) || !candidate->output.resize(state_bytes)) {
        discard();
        return ERR_OUT_OF_MEMORY;
    }
    for (auto &entry : candidate->table.span()) { entry = 0; }
    candidate->fields = descriptors;
    auto created = superpos::Schema::create(p_schema_id, std::span(candidate->fields).first(field_count));
    if (!created || created->state_bytes() != state_bytes) { discard(); return ERR_INVALID_DATA; }
    candidate->schema.emplace(*created);
    candidate->adapter.schema = &*candidate->schema;
    candidate->adapter.mask = mask;
    candidate->session = p_session->get_instance_id();
    candidate->schema_id = p_schema_id;
    candidate->binding = binding;
    candidate->capacity = uint32_t(capacity);
    candidate->maximum_objects = uint32_t(objects);
    candidate->maximum_bracket = uint64_t(bracket);
    candidate->state_bytes = state_bytes;
    candidate->block_bytes = block;
    if (state) { state->~State(); Memory::free_static(state); }
    state = candidate;
    return OK;
}

Error SuperposStateHistory::record_object(uint64_t p_handle) {
    const Error ready = _preflight();
    if (ready != OK) { return ready; }
    Ref<SuperposSession> session(Object::cast_to<SuperposSession>(ObjectDB::get_instance(state->session)));
    if (session.is_null()) { return ERR_UNCONFIGURED; }
    uint64_t binding = 0;
    Error error = session->query_binding_generation(binding);
    if (error != OK) { return error; }
    if (!state || binding != state->binding) { return ERR_UNCONFIGURED; }
    SuperposReceiverPublicAccess::Image image;
    error = SuperposReceiverPublicAccess::world_image(*session.ptr(), p_handle, image, state->staging.bytes());
    if (error != OK) { return error; }
    if (image.schema != state->schema_id) { return ERR_INVALID_PARAMETER; }
    return state->push(p_handle, {}, 0, image.tick, image.revision);
}

Error SuperposStateHistory::record_replica(const Ref<SuperposReplicaView> &p_replica) {
    const Error ready = _preflight();
    if (ready != OK) { return ready; }
    if (p_replica.is_null()) { return ERR_INVALID_PARAMETER; }
    SuperposReceiverPublicAccess::Image image;
    // The staged copy is validated against this schema's exact size first.
    if (p_replica->get_binding_generation() == 0) { return ERR_INVALID_PARAMETER; }
    const Error error = SuperposReceiverPublicAccess::replica_image(*p_replica.ptr(), image, state->staging.bytes());
    if (error != OK) { return error == ERR_INVALID_DATA ? ERR_INVALID_PARAMETER : error; }
    if (!state || image.session != state->session) { return ERR_INVALID_PARAMETER; }
    if (image.schema != state->schema_id) { return ERR_INVALID_PARAMETER; }
    return state->push(image.handle, image.identity, image.binding, image.tick, image.revision);
}

Dictionary SuperposStateHistory::read_exact(uint64_t p_handle, uint64_t p_tick, const PackedInt64Array &p_fields) {
    Dictionary result;
    Error error = _preflight();
    HistoryTrack *track = error == OK ? state->find(p_handle) : nullptr;
    if (error == OK && !track) { error = ERR_DOES_NOT_EXIST; }
    result["error"] = error;
    if (error != OK) { return result; }
    auto view = track->history->exact(track->epoch, p_tick);
    if (!view) { result["error"] = history_error(view.error(), true); return result; }
    return state->decode(view->canonical, p_tick, p_fields);
}

Dictionary SuperposStateHistory::sample(uint64_t p_handle, uint64_t p_tick, double p_fraction, const PackedInt64Array &p_fields) {
    Dictionary result;
    Error error = _preflight();
    if (error == OK && (!std::isfinite(p_fraction) || p_fraction < 0.0 || p_fraction >= 1.0)) { error = ERR_INVALID_PARAMETER; }
    HistoryTrack *track = error == OK ? state->find(p_handle) : nullptr;
    if (error == OK && !track) { error = ERR_DOES_NOT_EXIST; }
    result["error"] = error;
    if (error != OK) { return result; }
    auto sampled = track->history->sample(track->epoch, p_tick, p_fraction, state->output.bytes());
    if (!sampled) { result["error"] = history_error(sampled.error(), true); return result; }
    result = state->decode(state->output.bytes(), p_tick, p_fields);
    if (int(result["error"]) == OK) { result["fraction"] = p_fraction; }
    return result;
}

Dictionary SuperposStateHistory::read_object_history(uint64_t p_handle) const {
    Dictionary result;
    Error error = _preflight();
    const HistoryTrack *track = error == OK ? state->find(p_handle) : nullptr;
    if (error == OK && !track) { error = ERR_DOES_NOT_EXIST; }
    result["error"] = error;
    if (error != OK) { return result; }
    PackedInt64Array ticks;
    if (ticks.resize(int(track->count)) != OK) { result["error"] = ERR_OUT_OF_MEMORY; return result; }
    for (uint32_t i = 0; i < track->count; ++i) { ticks.set(int(i), superpos_egp::signed_bits(track->ticks[(track->head + i) % state->capacity])); }
    result["ticks"] = ticks;
    result["samples"] = int64_t(track->count);
    result["history_epoch"] = superpos_egp::signed_bits(track->epoch);
    return result;
}

Error SuperposStateHistory::forget(uint64_t p_handle) {
    const Error ready = _preflight();
    if (ready != OK) { return ready; }
    if (!state->find(p_handle)) { return ERR_DOES_NOT_EXIST; }
    state->erase(p_handle);
    return OK;
}

Error SuperposStateHistory::clear() {
    const Error ready = _preflight();
    if (ready != OK) { return ready; }
    for (auto &track : state->tracks.span()) { if (track.used) { state->release(track); } }
    for (auto &entry : state->table.span()) { entry = 0; }
    state->live = 0;
    return OK;
}

Dictionary SuperposStateHistory::read_status() const {
    Dictionary result;
    result["error"] = _preflight();
    if (int(result["error"]) != OK) { return result; }
    result["schema_id"] = superpos_egp::signed_bits(state->schema_id);
    result["capacity"] = int64_t(state->capacity);
    result["maximum_objects"] = int64_t(state->maximum_objects);
    result["maximum_bracket_ticks"] = superpos_egp::signed_bits(state->maximum_bracket);
    result["objects"] = int64_t(state->live);
    result["state_bytes"] = int64_t(state->state_bytes);
    result["interpolated_fields"] = int64_t(std::popcount(state->adapter.mask));
    result["recorded"] = state->recorded;
    result["duplicates"] = state->duplicates;
    result["rejected"] = state->rejected;
    result["history_resets"] = state->resets;
    result["charged_bytes"] = int64_t(state->quota.used(superpos::MemoryDomain::History));
    return result;
}

void SuperposStateHistory::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "session", "schema_id", "configuration"), &SuperposStateHistory::configure, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("record_object", "handle"), &SuperposStateHistory::record_object);
    ClassDB::bind_method(D_METHOD("record_replica", "replica"), &SuperposStateHistory::record_replica);
    ClassDB::bind_method(D_METHOD("read_exact", "handle", "tick", "fields"), &SuperposStateHistory::read_exact, DEFVAL(PackedInt64Array()));
    ClassDB::bind_method(D_METHOD("sample", "handle", "tick", "fraction", "fields"), &SuperposStateHistory::sample, DEFVAL(PackedInt64Array()));
    ClassDB::bind_method(D_METHOD("read_object_history", "handle"), &SuperposStateHistory::read_object_history);
    ClassDB::bind_method(D_METHOD("forget", "handle"), &SuperposStateHistory::forget);
    ClassDB::bind_method(D_METHOD("clear"), &SuperposStateHistory::clear);
    ClassDB::bind_method(D_METHOD("read_status"), &SuperposStateHistory::read_status);
}
