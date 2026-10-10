// SPDX-License-Identifier: MIT
#include "superpos_snapshot_interpolator.h"
#include "u64_bits.h"
#include "core/math/basis.h"
#include "core/math/math_funcs.h"
#include "core/math/quaternion.h"
#include "core/object/class_db.h"
#include "core/os/memory.h"
#include "private/charged_array.hpp"
#include "private/module_memory.hpp"
#include "superpos/allocator.hpp"
#include "superpos/gameplay.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>

namespace {
constexpr size_t pose_values = 18;
constexpr size_t pose_bytes = pose_values * 8;
constexpr uint32_t maximum_entities_limit = 4096;
constexpr uint32_t maximum_capacity_limit = 256;
constexpr size_t maximum_budget = size_t(256) << 20;
constexpr size_t allocation_slack = 128;

struct Pose {
    Transform3D transform;
    Vector3 velocity, angular;
};

void encode_pose(const Pose &p_pose, std::span<std::byte> r_bytes) noexcept {
    const Basis &b = p_pose.transform.basis;
    const double values[pose_values] = {
        double(b.rows[0].x), double(b.rows[0].y), double(b.rows[0].z),
        double(b.rows[1].x), double(b.rows[1].y), double(b.rows[1].z),
        double(b.rows[2].x), double(b.rows[2].y), double(b.rows[2].z),
        double(p_pose.transform.origin.x), double(p_pose.transform.origin.y), double(p_pose.transform.origin.z),
        double(p_pose.velocity.x), double(p_pose.velocity.y), double(p_pose.velocity.z),
        double(p_pose.angular.x), double(p_pose.angular.y), double(p_pose.angular.z)};
    for (size_t i = 0; i < pose_values; ++i) {
        const uint64_t bits = std::bit_cast<uint64_t>(values[i]);
        for (size_t k = 0; k < 8; ++k) { r_bytes[i * 8 + k] = std::byte(uint8_t(bits >> (8 * k))); }
    }
}

bool pose_valid(const Pose &p_pose) noexcept {
    if (!p_pose.transform.is_finite() || !p_pose.velocity.is_finite() || !p_pose.angular.is_finite()) { return false; }
    if (double(p_pose.angular.length_squared()) > 1000000.0) { return false; }
    return std::abs(double(p_pose.transform.basis.determinant())) > 1e-12;
}

bool decode_pose(std::span<const std::byte> p_bytes, Pose &r_pose) noexcept {
    if (p_bytes.size() != pose_bytes) { return false; }
    double values[pose_values];
    for (size_t i = 0; i < pose_values; ++i) {
        uint64_t bits = 0;
        for (size_t k = 0; k < 8; ++k) { bits |= uint64_t(uint8_t(p_bytes[i * 8 + k])) << (8 * k); }
        values[i] = std::bit_cast<double>(bits);
        if (!std::isfinite(values[i])) { return false; }
    }
    Pose decoded;
    decoded.transform.basis = Basis(real_t(values[0]), real_t(values[1]), real_t(values[2]), real_t(values[3]), real_t(values[4]),
        real_t(values[5]), real_t(values[6]), real_t(values[7]), real_t(values[8]));
    decoded.transform.origin = Vector3(real_t(values[9]), real_t(values[10]), real_t(values[11]));
    decoded.velocity = Vector3(real_t(values[12]), real_t(values[13]), real_t(values[14]));
    decoded.angular = Vector3(real_t(values[15]), real_t(values[16]), real_t(values[17]));
    if (!pose_valid(decoded)) { return false; }
    r_pose = decoded;
    return true;
}

// Pure, bounded pose transform for core SnapshotHistory. It retains no span
// and touches no scene, solver or canonical state.
class PoseAdapter final : public superpos::SimulationAdapter {
public:
    superpos::SimulationDescriptor descriptor() const noexcept override {
        return {pose_bytes, 0, superpos::SimulationCapability::Interpolation};
    }
    superpos::Status validate_state(std::span<const std::byte> p_bytes) const noexcept override {
        Pose pose;
        return decode_pose(p_bytes, pose) ? superpos::Status{} : superpos::fail(superpos::Error::InvalidArgument);
    }
    superpos::Status validate_input(std::span<const std::byte>) const noexcept override { return superpos::fail(superpos::Error::Unsupported); }
    superpos::Status step(superpos::Tick, std::span<const std::byte>, std::span<const std::byte>, std::span<std::byte>) noexcept override {
        return superpos::fail(superpos::Error::Unsupported);
    }
    superpos::Status interpolate(std::span<const std::byte> p_a, std::span<const std::byte> p_b, double p_alpha,
            std::span<std::byte> r_output) noexcept override {
        Pose a, b;
        if (!std::isfinite(p_alpha) || p_alpha < 0.0 || p_alpha > 1.0 || r_output.size() != pose_bytes ||
                !decode_pose(p_a, a) || !decode_pose(p_b, b)) {
            return superpos::fail(superpos::Error::InvalidArgument);
        }
        Pose result;
        result.transform = a.transform.interpolate_with(b.transform, real_t(p_alpha));
        result.velocity = a.velocity.lerp(b.velocity, real_t(p_alpha));
        result.angular = a.angular.lerp(b.angular, real_t(p_alpha));
        encode_pose(result, r_output);
        return {};
    }
};

Error engine_error(superpos::Error p_error) {
    switch (p_error) {
        case superpos::Error::None: return OK;
        case superpos::Error::OutOfMemory: case superpos::Error::CapacityExceeded: return ERR_OUT_OF_MEMORY;
        case superpos::Error::NotReady: return ERR_UNCONFIGURED;
        case superpos::Error::StaleEpoch: case superpos::Error::ProtocolViolation: return ERR_INVALID_DATA;
        case superpos::Error::RecoveryUnavailable: return ERR_DOES_NOT_EXIST;
        case superpos::Error::Unsupported: return ERR_UNAVAILABLE;
        case superpos::Error::InvalidArgument: return ERR_INVALID_PARAMETER;
        case superpos::Error::Overflow: case superpos::Error::CounterExhausted: return ERR_PARAMETER_RANGE_ERROR;
        default: return ERR_INVALID_DATA;
    }
}

// Signed distance a - b in ticks. Only bounded relative distances are used.
double ticks_between(uint64_t p_a, double p_a_fraction, uint64_t p_b, double p_b_fraction) noexcept {
    const double whole = p_a >= p_b ? double(p_a - p_b) : -double(p_b - p_a);
    return whole + (p_a_fraction - p_b_fraction);
}

uint64_t mix(uint64_t p_value) noexcept {
    p_value += 0x9e3779b97f4a7c15ULL;
    p_value = (p_value ^ (p_value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    p_value = (p_value ^ (p_value >> 27)) * 0x94d049bb133111ebULL;
    return p_value ^ (p_value >> 31);
}

enum class Mode : uint8_t { Interpolated, Extrapolated, Held, Priming, GapHold };
const char *mode_name(Mode p_mode) {
    switch (p_mode) {
        case Mode::Interpolated: return "interpolated";
        case Mode::Extrapolated: return "extrapolated";
        case Mode::Held: return "held";
        case Mode::Priming: return "priming";
        case Mode::GapHold: return "gap_hold";
    }
    return "held";
}

struct Track {
    uint64_t entity = 0;
    bool used = false;
    void *block = nullptr;
    std::optional<superpos::SnapshotHistory> history;
    uint64_t *ticks = nullptr;
    uint32_t head = 0, count = 0;
    uint64_t epoch = 0;
    superpos::Epoch core_epoch = 1;
    uint64_t revision = 0;
    // Presentation correction: offset plus rotation quaternion (x, y, z, w).
    double correction[3] = {0.0, 0.0, 0.0};
    double rotation[4] = {0.0, 0.0, 0.0, 1.0};
    int64_t interpolated = 0, extrapolated = 0, held = 0, held_moving = 0, held_stationary = 0, priming = 0, gap_holds = 0;
    int64_t corrections = 0, epoch_resets = 0, teleports = 0, rejected = 0;
    double largest_correction = 0.0, largest_angular = 0.0;
};
} // namespace

struct SuperposSnapshotInterpolator::State {
    superpos::QuotaAllocator quota;
    superpos_egp::ChargedArray<Track> tracks{quota, superpos::MemoryDomain::History};
    superpos_egp::ChargedArray<uint32_t> table{quota, superpos::MemoryDomain::History};
    PoseAdapter adapter;
    double tick_rate = 60.0, base_delay = 6.0, max_delay = 6.0, delay = 6.0, extrapolation = 6.0;
    double teleport_distance = 8.0, correction_decay = 12.0;
    uint64_t maximum_bracket = 60;
    uint32_t capacity = 32, maximum_entities = 1024, live = 0;
    size_t block_bytes = 0;
    bool started = false;
    uint64_t clock_tick = 0, newest_tick = 0;
    double clock_fraction = 0.0, since_arrival = 0.0, cadence_peak = 0.0;
    int64_t interpolated = 0, extrapolated = 0, held = 0, priming = 0, gap_holds = 0, corrections = 0, teleports = 0;
    int64_t epoch_resets = 0, rejected_epochs = 0, rejected_stale = 0, clock_holds = 0, clock_resyncs = 0;
    double largest_correction = 0.0, largest_angular = 0.0;

    ~State() {
        for (auto &track : tracks.span()) { release(track); }
    }
    void release(Track &p_track) noexcept {
        p_track.history.reset();
        if (p_track.block) { quota.deallocate(p_track.block); }
        p_track = Track{};
    }
    size_t mask() const noexcept { return table.size() - 1; }
    // Returns the table position of an entity, or the empty position to insert.
    size_t probe(uint64_t p_entity, bool &r_found) const noexcept {
        size_t position = size_t(mix(p_entity)) & mask();
        while (true) {
            const uint32_t entry = table.span()[position];
            if (!entry) { r_found = false; return position; }
            if (tracks.span()[entry - 1].entity == p_entity) { r_found = true; return position; }
            position = (position + 1) & mask();
        }
    }
    Track *find(uint64_t p_entity) noexcept {
        bool found = false;
        const size_t position = probe(p_entity, found);
        return found ? &tracks[table[position] - 1] : nullptr;
    }
    const Track *find(uint64_t p_entity) const noexcept {
        bool found = false;
        const size_t position = probe(p_entity, found);
        return found ? &tracks.span()[table.span()[position] - 1] : nullptr;
    }
    void erase(uint64_t p_entity) noexcept {
        bool found = false;
        size_t hole = probe(p_entity, found);
        if (!found) { return; }
        release(tracks[table[hole] - 1]);
        table[hole] = 0;
        --live;
        // Backward-shift deletion keeps every probe chain contiguous.
        size_t next = hole;
        while (true) {
            next = (next + 1) & mask();
            const uint32_t entry = table[next];
            if (!entry) { break; }
            const size_t home = size_t(mix(tracks[entry - 1].entity)) & mask();
            const bool between = hole <= next ? (hole < home && home <= next) : (hole < home || home <= next);
            if (!between) {
                table[hole] = entry;
                table[next] = 0;
                hole = next;
            }
        }
    }
    Error create(uint64_t p_entity, Track *&r_track) noexcept {
        if (live >= maximum_entities) { return ERR_PARAMETER_RANGE_ERROR; }
        uint32_t slot = 0;
        while (slot < tracks.size() && tracks[slot].used) { ++slot; }
        if (slot == tracks.size()) { return ERR_PARAMETER_RANGE_ERROR; }
        void *block = quota.allocate(block_bytes, alignof(std::max_align_t), superpos::MemoryDomain::History);
        if (!block) { return ERR_OUT_OF_MEMORY; }
        auto *bytes = static_cast<std::byte *>(block);
        auto *frames = reinterpret_cast<superpos::HistoryFrame *>(bytes);
        const size_t frame_bytes = size_t(capacity) * sizeof(superpos::HistoryFrame);
        const size_t arena_bytes = size_t(capacity) * pose_bytes;
        for (uint32_t i = 0; i < capacity; ++i) { std::construct_at(frames + i); }
        std::span<superpos::HistoryFrame> frame_span(frames, capacity);
        std::span<std::byte> arena(bytes + frame_bytes, arena_bytes);
        std::span<std::byte> scratch(bytes + frame_bytes + arena_bytes, pose_bytes);
        auto *ticks = reinterpret_cast<uint64_t *>(bytes + frame_bytes + arena_bytes + pose_bytes);
        auto created = superpos::SnapshotHistory::create({1, capacity, maximum_bracket}, adapter, frame_span, arena, scratch);
        if (!created) { quota.deallocate(block); return engine_error(created.error()); }
        Track &track = tracks[slot];
        track = Track{};
        track.entity = p_entity;
        track.used = true;
        track.block = block;
        track.history.emplace(std::move(*created));
        track.ticks = ticks;
        bool found = false;
        const size_t position = probe(p_entity, found);
        table[position] = slot + 1;
        ++live;
        r_track = &track;
        return OK;
    }
    void set_clock_behind(uint64_t p_tick) noexcept {
        const double whole = std::floor(delay);
        const double part = delay - whole;
        const uint64_t steps = uint64_t(whole);
        if (p_tick < steps + (part > 0.0 ? 1 : 0)) { clock_tick = 0; clock_fraction = 0.0; return; }
        if (part > 0.0) { clock_tick = p_tick - steps - 1; clock_fraction = 1.0 - part; }
        else { clock_tick = p_tick - steps; clock_fraction = 0.0; }
    }
    Error exact(const Track &p_track, uint64_t p_tick, Pose &r_pose) const noexcept {
        auto view = p_track.history->exact(p_track.core_epoch, p_tick);
        if (!view) { return engine_error(view.error()); }
        return decode_pose(view->canonical, r_pose) ? OK : ERR_INVALID_DATA;
    }
    Error evaluate(Track &p_track, uint64_t p_tick, double p_fraction, Pose &r_pose, Mode &r_mode) noexcept {
        if (!p_track.count || !p_track.history) { return ERR_UNCONFIGURED; }
        const uint64_t first = p_track.ticks[p_track.head];
        const uint64_t newest = p_track.ticks[(p_track.head + p_track.count - 1) % capacity];
        if (p_tick < first) { r_mode = Mode::Priming; return exact(p_track, first, r_pose); }
        if (p_tick > newest || (p_tick == newest && p_fraction > 0.0)) {
            const double age = ticks_between(p_tick, p_fraction, newest, 0.0);
            r_mode = age > extrapolation ? Mode::Held : Mode::Extrapolated;
            Pose latest;
            const Error error = exact(p_track, newest, latest);
            if (error != OK) { return error; }
            const double duration = std::min(std::max(age, 0.0), extrapolation) / tick_rate;
            latest.transform.origin += latest.velocity * real_t(duration);
            const double speed = double(latest.angular.length());
            if (speed > 0.000001 && duration > 0.0) {
                // World-space angular velocity in radians per second. Left
                // multiplication preserves nonuniform basis scale.
                latest.transform.basis = Basis(Quaternion(latest.angular / real_t(speed), real_t(speed * duration))) * latest.transform.basis;
            }
            r_pose = latest;
            return OK;
        }
        std::array<std::byte, pose_bytes> staged{};
        auto sampled = p_track.history->sample(p_track.core_epoch, p_tick, p_fraction, staged);
        if (sampled) {
            r_mode = Mode::Interpolated;
            return decode_pose(staged, r_pose) ? OK : ERR_INVALID_DATA;
        }
        if (sampled.error() != superpos::Error::RecoveryUnavailable) { return engine_error(sampled.error()); }
        // The bracket is wider than the maximum gap: hold the earlier sample.
        uint64_t before = first;
        for (uint32_t i = 0; i < p_track.count; ++i) {
            const uint64_t value = p_track.ticks[(p_track.head + i) % capacity];
            if (value <= p_tick) { before = value; } else { break; }
        }
        r_mode = Mode::GapHold;
        return exact(p_track, before, r_pose);
    }
    static void apply_correction(const Track &p_track, Pose &r_pose) noexcept {
        r_pose.transform.origin += Vector3(real_t(p_track.correction[0]), real_t(p_track.correction[1]), real_t(p_track.correction[2]));
        const Quaternion rotation(real_t(p_track.rotation[0]), real_t(p_track.rotation[1]), real_t(p_track.rotation[2]), real_t(p_track.rotation[3]));
        r_pose.transform.basis = Basis(rotation) * r_pose.transform.basis;
    }
};

void SuperposSnapshotInterpolator::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "configuration"), &SuperposSnapshotInterpolator::configure, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("submit", "entity", "tick", "pose", "velocity", "angular_velocity", "discontinuity_epoch"),
        &SuperposSnapshotInterpolator::submit, DEFVAL(Vector3()), DEFVAL(Vector3()), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("advance", "delta"), &SuperposSnapshotInterpolator::advance);
    ClassDB::bind_method(D_METHOD("read_clock"), &SuperposSnapshotInterpolator::read_clock);
    ClassDB::bind_method(D_METHOD("sample", "entity", "delta"), &SuperposSnapshotInterpolator::sample, DEFVAL(0.0));
    ClassDB::bind_method(D_METHOD("sample_at", "entity", "tick", "fraction"), &SuperposSnapshotInterpolator::sample_at, DEFVAL(0.0));
    ClassDB::bind_method(D_METHOD("read_entity", "entity"), &SuperposSnapshotInterpolator::read_entity);
    ClassDB::bind_method(D_METHOD("read_statistics"), &SuperposSnapshotInterpolator::read_statistics);
    ClassDB::bind_method(D_METHOD("remove", "entity"), &SuperposSnapshotInterpolator::remove);
    ClassDB::bind_method(D_METHOD("clear"), &SuperposSnapshotInterpolator::clear);
}

SuperposSnapshotInterpolator::~SuperposSnapshotInterpolator() {
    if (state) { state->~State(); Memory::free_static(state); state = nullptr; }
}

Error SuperposSnapshotInterpolator::_preflight() const {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    return state ? OK : ERR_UNCONFIGURED;
}

Error SuperposSnapshotInterpolator::configure(const Dictionary &p_configuration) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    static const char *allowed[] = {"tick_rate", "delay_seconds", "max_extrapolation_seconds", "max_adaptive_delay_seconds",
        "teleport_distance", "capacity", "maximum_entities", "maximum_gap_seconds", "correction_decay"};
    for (int i = 0; i < p_configuration.size(); ++i) {
        const Variant key = p_configuration.get_key_at_index(i);
        if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) { return ERR_INVALID_PARAMETER; }
        bool known = false;
        for (const char *name : allowed) { known = known || String(key) == name; }
        const Variant::Type type = p_configuration.get_value_at_index(i).get_type();
        if (!known || (type != Variant::INT && type != Variant::FLOAT)) { return ERR_INVALID_PARAMETER; }
    }
    auto number = [&](const char *p_key, double p_default) { return p_configuration.has(p_key) ? double(p_configuration[p_key]) : p_default; };
    const double rate = number("tick_rate", 60.0), delay = number("delay_seconds", 0.1), extrapolation = number("max_extrapolation_seconds", 0.1);
    const double adaptive = number("max_adaptive_delay_seconds", 0.0), teleport = number("teleport_distance", 8.0);
    const double gap = number("maximum_gap_seconds", 1.0), decay = number("correction_decay", 12.0);
    const double capacity = number("capacity", 32), entities = number("maximum_entities", 1024);
    for (double value : {rate, delay, extrapolation, adaptive, teleport, gap, decay, capacity, entities}) {
        if (!std::isfinite(value)) { return ERR_INVALID_PARAMETER; }
    }
    if (rate < 1.0 || rate > 1000.0 || delay < 0.0 || delay > 2.0 || extrapolation < 0.0 || extrapolation > 0.5 ||
            adaptive < 0.0 || adaptive > 2.0 || (adaptive > 0.0 && adaptive < delay) || teleport < 0.0 || teleport > 1000000.0 ||
            gap <= 0.0 || gap > 10.0 || decay < 0.0 || decay > 100.0 || capacity != std::floor(capacity) || capacity < 2 ||
            capacity > maximum_capacity_limit || entities != std::floor(entities) || entities < 1 || entities > maximum_entities_limit) {
        return ERR_INVALID_PARAMETER;
    }
    const uint32_t ring = uint32_t(capacity), maximum = uint32_t(entities);
    const size_t block = size_t(ring) * (sizeof(superpos::HistoryFrame) + pose_bytes + sizeof(uint64_t)) + pose_bytes;
    size_t table_size = 1;
    while (table_size < size_t(maximum) * 2) { table_size <<= 1; }
    const size_t budget = size_t(maximum) * (block + allocation_slack) + size_t(maximum) * sizeof(Track) +
        table_size * sizeof(uint32_t) + 4 * allocation_slack;
    if (budget > maximum_budget) { return ERR_PARAMETER_RANGE_ERROR; }
    void *memory = Memory::alloc_static(sizeof(State));
    if (!memory) { return ERR_OUT_OF_MEMORY; }
    State *candidate = std::construct_at(static_cast<State *>(memory));
    auto discard = [&]() { candidate->~State(); Memory::free_static(candidate); };
    superpos::MemoryPlan plan;
    plan.limits.fill(0);
    plan.limits[size_t(superpos::MemoryDomain::History)] = budget;
    if (!candidate->quota.bind(superpos_egp::module_backing(), plan) || !candidate->tracks.initialize(maximum) ||
            !candidate->table.initialize(table_size)) {
        discard();
        return ERR_OUT_OF_MEMORY;
    }
    for (auto &entry : candidate->table.span()) { entry = 0; }
    candidate->tick_rate = rate;
    candidate->base_delay = delay * rate;
    candidate->max_delay = adaptive > 0.0 ? adaptive * rate : candidate->base_delay;
    candidate->delay = candidate->base_delay;
    candidate->extrapolation = extrapolation * rate;
    candidate->teleport_distance = teleport;
    candidate->correction_decay = decay;
    candidate->maximum_bracket = std::max<uint64_t>(1, uint64_t(std::ceil(gap * rate)));
    candidate->capacity = ring;
    candidate->maximum_entities = maximum;
    candidate->block_bytes = block;
    if (state) { state->~State(); Memory::free_static(state); }
    state = candidate;
    return OK;
}

Error SuperposSnapshotInterpolator::submit(uint64_t p_entity, uint64_t p_tick, const Transform3D &p_pose,
        const Vector3 &p_velocity, const Vector3 &p_angular_velocity, uint64_t p_discontinuity_epoch) {
    const Error ready = _preflight();
    if (ready != OK) { return ready; }
    Pose incoming{p_pose, p_velocity, p_angular_velocity};
    if (!p_entity || !pose_valid(incoming)) { return ERR_INVALID_PARAMETER; }
    State &s = *state;
    Track *track = s.find(p_entity);
    const bool fresh = !track;
    if (fresh) {
        const Error created = s.create(p_entity, track);
        if (created != OK) { return created; }
        track->epoch = p_discontinuity_epoch;
    }
    if (p_discontinuity_epoch < track->epoch) { ++s.rejected_epochs; ++track->rejected; return ERR_INVALID_DATA; }
    const bool explicit_reset = track->count && p_discontinuity_epoch > track->epoch;
    const uint64_t newest = track->count ? track->ticks[(track->head + track->count - 1) % s.capacity] : 0;
    if (track->count && !explicit_reset && p_tick < newest) { ++s.rejected_stale; ++track->rejected; return ERR_INVALID_DATA; }
    if (track->revision == UINT64_MAX || track->core_epoch == UINT64_MAX) { return ERR_PARAMETER_RANGE_ERROR; }
    bool reset = explicit_reset;
    Pose latest;
    if (track->count && !reset && s.teleport_distance > 0.0) {
        const Error read = s.exact(*track, newest, latest);
        if (read != OK) { return read; }
        if (double(latest.transform.origin.distance_to(p_pose.origin)) > s.teleport_distance) { reset = true; }
    }
    if (track->count && !explicit_reset && p_tick > newest && s.max_delay > s.base_delay) {
        // Authoritative sample spacing, not RTT: a bounded margin absorbs sparse
        // interest and loss without moving the playback clock backward.
        s.cadence_peak = std::max(s.cadence_peak, std::min(double(p_tick - newest), s.max_delay));
    }
    const bool was_extrapolating = track->count && !reset && s.started &&
        (s.clock_tick > newest || (s.clock_tick == newest && s.clock_fraction > 0.0));
    Pose before;
    Mode mode = Mode::Held;
    if (was_extrapolating) {
        const Error evaluated = s.evaluate(*track, s.clock_tick, s.clock_fraction, before, mode);
        if (evaluated != OK) { return evaluated; }
    }
    std::array<std::byte, pose_bytes> encoded{};
    encode_pose(incoming, encoded);
    if (reset) {
        const superpos::Epoch next = track->core_epoch + 1;
        auto cleared = track->history->reset_epoch(next);
        if (!cleared) { return engine_error(cleared.error()); }
        track->core_epoch = next;
        track->head = track->count = 0;
        track->correction[0] = track->correction[1] = track->correction[2] = 0.0;
        track->rotation[0] = track->rotation[1] = track->rotation[2] = 0.0;
        track->rotation[3] = 1.0;
        if (explicit_reset) { ++s.epoch_resets; ++track->epoch_resets; } else { ++s.teleports; ++track->teleports; }
    }
    auto pushed = track->history->push(track->core_epoch, p_tick, track->revision + 1, encoded);
    if (!pushed) {
        if (fresh) { s.erase(p_entity); }
        return engine_error(pushed.error());
    }
    ++track->revision;
    track->epoch = p_discontinuity_epoch;
    const bool replaced = track->count && track->ticks[(track->head + track->count - 1) % s.capacity] == p_tick;
    if (!replaced) {
        if (track->count == s.capacity) { track->head = (track->head + 1) % s.capacity; --track->count; }
        track->ticks[(track->head + track->count) % s.capacity] = p_tick;
        ++track->count;
    }
    if (was_extrapolating) {
        // Re-entry after an interest gap must not snap the displayed body
        // backward; the presentation offset decays during later samples.
        Pose after;
        if (s.evaluate(*track, s.clock_tick, s.clock_fraction, after, mode) == OK) {
            const Vector3 error = before.transform.origin - after.transform.origin;
            Quaternion rotation_error = (before.transform.basis.get_rotation_quaternion() *
                after.transform.basis.get_rotation_quaternion().inverse()).normalized();
            if (rotation_error.w < 0) { rotation_error = -rotation_error; }
            const double angular_error = double(Quaternion().angle_to(rotation_error));
            if (double(error.length_squared()) > 0.000001 || angular_error > 0.00001) {
                track->correction[0] += double(error.x);
                track->correction[1] += double(error.y);
                track->correction[2] += double(error.z);
                Quaternion accumulated(real_t(track->rotation[0]), real_t(track->rotation[1]), real_t(track->rotation[2]), real_t(track->rotation[3]));
                accumulated = (accumulated * rotation_error).normalized();
                if (accumulated.w < 0) { accumulated = -accumulated; }
                track->rotation[0] = accumulated.x; track->rotation[1] = accumulated.y;
                track->rotation[2] = accumulated.z; track->rotation[3] = accumulated.w;
                const double distance = double(error.length());
                s.largest_correction = std::max(s.largest_correction, distance);
                s.largest_angular = std::max(s.largest_angular, angular_error);
                track->largest_correction = std::max(track->largest_correction, distance);
                track->largest_angular = std::max(track->largest_angular, angular_error);
                ++s.corrections;
                ++track->corrections;
            }
        }
    }
    if (!s.started || p_tick > s.newest_tick) {
        s.newest_tick = p_tick;
        s.since_arrival = 0.0;
        if (!s.started) { s.set_clock_behind(p_tick); s.started = true; }
    }
    return OK;
}

Error SuperposSnapshotInterpolator::advance(double p_delta) {
    const Error ready = _preflight();
    if (ready != OK) { return ready; }
    if (!std::isfinite(p_delta) || p_delta < 0.0) { return ERR_INVALID_PARAMETER; }
    State &s = *state;
    if (!s.started) { return OK; }
    const double delta = std::min(p_delta, 0.25);
    s.since_arrival += delta;
    s.cadence_peak = std::max(0.0, s.cadence_peak - delta * s.tick_rate * 0.05);
    s.delay = std::clamp(s.cadence_peak + 3.0, s.base_delay, s.max_delay);
    const double behind = ticks_between(s.newest_tick, 0.0, s.clock_tick, s.clock_fraction);
    const double desired = behind + std::min(s.since_arrival * s.tick_rate, 3.0) - s.delay;
    if (desired > std::max(4.0 * s.max_delay, 2.0 * s.tick_rate)) {
        // Authority time jumped far ahead (resumed stream or long stall).
        s.set_clock_behind(s.newest_tick);
        ++s.clock_resyncs;
        return OK;
    }
    const double speed = std::clamp(1.0 + desired * 0.025, 0.9, 1.1);
    const double step = std::min(delta * s.tick_rate * speed, std::max(behind, 0.0));
    if (delta > 0.0 && step <= 0.0) { ++s.clock_holds; return OK; }
    double fraction = s.clock_fraction + step;
    const double whole = std::floor(fraction);
    fraction -= whole;
    uint64_t tick = 0;
    if (!superpos_egp::checked_add(s.clock_tick, uint64_t(whole), tick)) { tick = UINT64_MAX; fraction = 0.0; }
    if (tick > s.newest_tick || (tick == s.newest_tick && fraction > 0.0)) { tick = s.newest_tick; fraction = 0.0; }
    s.clock_tick = tick;
    s.clock_fraction = fraction;
    return OK;
}

Dictionary SuperposSnapshotInterpolator::read_clock() const {
    Dictionary result;
    result["error"] = _preflight();
    if (int(result["error"]) != OK) { return result; }
    const State &s = *state;
    result["started"] = s.started;
    result["tick"] = superpos_egp::signed_bits(s.clock_tick);
    result["fraction"] = s.clock_fraction;
    result["newest_tick"] = superpos_egp::signed_bits(s.newest_tick);
    result["buffer_ticks"] = s.started ? ticks_between(s.newest_tick, 0.0, s.clock_tick, s.clock_fraction) : 0.0;
    result["target_delay_seconds"] = s.delay / s.tick_rate;
    return result;
}

Dictionary SuperposSnapshotInterpolator::sample(uint64_t p_entity, double p_delta) {
    Dictionary result;
    Error error = _preflight();
    if (error == OK && (!std::isfinite(p_delta) || p_delta < 0.0)) { error = ERR_INVALID_PARAMETER; }
    State *s = state;
    Track *track = error == OK ? s->find(p_entity) : nullptr;
    if (error == OK && !track) { error = ERR_DOES_NOT_EXIST; }
    result["error"] = error;
    if (error != OK) { return result; }
    Pose pose;
    Mode mode = Mode::Held;
    error = s->evaluate(*track, s->clock_tick, s->clock_fraction, pose, mode);
    result["error"] = error;
    if (error != OK) { return result; }
    switch (mode) {
        case Mode::Interpolated: ++s->interpolated; ++track->interpolated; break;
        case Mode::Extrapolated: ++s->extrapolated; ++track->extrapolated; break;
        case Mode::Priming: ++s->priming; ++track->priming; break;
        case Mode::GapHold: ++s->gap_holds; ++track->gap_holds; break;
        case Mode::Held:
            ++s->held; ++track->held;
            if (double(pose.velocity.length_squared()) > 0.000001 || double(pose.angular.length_squared()) > 0.000001) { ++track->held_moving; }
            else { ++track->held_stationary; }
            break;
    }
    if (p_delta > 0.0 && s->correction_decay > 0.0) {
        const double keep = std::exp(-std::min(p_delta, 0.25) * s->correction_decay);
        for (double &value : track->correction) { value *= keep; }
        Quaternion rotation(real_t(track->rotation[0]), real_t(track->rotation[1]), real_t(track->rotation[2]), real_t(track->rotation[3]));
        rotation = rotation.slerp(Quaternion(), real_t(1.0 - keep)).normalized();
        track->rotation[0] = rotation.x; track->rotation[1] = rotation.y;
        track->rotation[2] = rotation.z; track->rotation[3] = rotation.w;
    }
    State::apply_correction(*track, pose);
    result["pose"] = pose.transform;
    result["velocity"] = pose.velocity;
    result["angular_velocity"] = pose.angular;
    result["mode"] = mode_name(mode);
    return result;
}

Dictionary SuperposSnapshotInterpolator::sample_at(uint64_t p_entity, uint64_t p_tick, double p_fraction) const {
    Dictionary result;
    Error error = _preflight();
    if (error == OK && (!std::isfinite(p_fraction) || p_fraction < 0.0 || p_fraction >= 1.0)) { error = ERR_INVALID_PARAMETER; }
    const Track *found = error == OK ? state->find(p_entity) : nullptr;
    if (error == OK && !found) { error = ERR_DOES_NOT_EXIST; }
    result["error"] = error;
    if (error != OK) { return result; }
    // Evaluation reads the track and uses only the history's private scratch.
    Pose pose;
    Mode mode = Mode::Held;
    error = state->evaluate(*const_cast<Track *>(found), p_tick, p_fraction, pose, mode);
    result["error"] = error;
    if (error != OK) { return result; }
    result["pose"] = pose.transform;
    result["velocity"] = pose.velocity;
    result["angular_velocity"] = pose.angular;
    result["mode"] = mode_name(mode);
    return result;
}

Dictionary SuperposSnapshotInterpolator::read_entity(uint64_t p_entity) const {
    Dictionary result;
    Error error = _preflight();
    const Track *track = error == OK ? state->find(p_entity) : nullptr;
    if (error == OK && !track) { error = ERR_DOES_NOT_EXIST; }
    result["error"] = error;
    if (error != OK) { return result; }
    PackedInt64Array ticks;
    if (ticks.resize(int(track->count)) != OK) { result["error"] = ERR_OUT_OF_MEMORY; return result; }
    for (uint32_t i = 0; i < track->count; ++i) { ticks.set(int(i), superpos_egp::signed_bits(track->ticks[(track->head + i) % state->capacity])); }
    result["ticks"] = ticks;
    result["discontinuity_epoch"] = superpos_egp::signed_bits(track->epoch);
    result["interpolated_samples"] = track->interpolated;
    result["extrapolated_samples"] = track->extrapolated;
    result["held_samples"] = track->held;
    result["held_moving_samples"] = track->held_moving;
    result["held_stationary_samples"] = track->held_stationary;
    result["priming_samples"] = track->priming;
    result["gap_hold_samples"] = track->gap_holds;
    result["corrections"] = track->corrections;
    result["epoch_resets"] = track->epoch_resets;
    result["teleports"] = track->teleports;
    result["rejected"] = track->rejected;
    result["largest_correction_m"] = track->largest_correction;
    result["largest_angular_correction_degrees"] = Math::rad_to_deg(track->largest_angular);
    result["correction_m"] = Vector3(real_t(track->correction[0]), real_t(track->correction[1]), real_t(track->correction[2])).length();
    return result;
}

Dictionary SuperposSnapshotInterpolator::read_statistics() const {
    Dictionary result;
    result["error"] = _preflight();
    if (int(result["error"]) != OK) { return result; }
    const State &s = *state;
    result["render_tick"] = superpos_egp::signed_bits(s.clock_tick);
    result["render_fraction"] = s.clock_fraction;
    result["buffer_ticks"] = s.started ? ticks_between(s.newest_tick, 0.0, s.clock_tick, s.clock_fraction) : 0.0;
    result["target_delay_seconds"] = s.delay / s.tick_rate;
    result["adaptive_buffering"] = s.max_delay > s.base_delay;
    result["tracked_entities"] = int64_t(s.live);
    result["maximum_entities"] = int64_t(s.maximum_entities);
    result["capacity"] = int64_t(s.capacity);
    result["interpolated_samples"] = s.interpolated;
    result["extrapolated_samples"] = s.extrapolated;
    result["held_samples"] = s.held;
    result["priming_samples"] = s.priming;
    result["gap_hold_samples"] = s.gap_holds;
    result["corrections"] = s.corrections;
    result["teleports"] = s.teleports;
    result["epoch_resets"] = s.epoch_resets;
    result["rejected_epochs"] = s.rejected_epochs;
    result["rejected_stale"] = s.rejected_stale;
    result["clock_hold_frames"] = s.clock_holds;
    result["clock_resyncs"] = s.clock_resyncs;
    result["largest_correction_m"] = s.largest_correction;
    result["largest_angular_correction_degrees"] = Math::rad_to_deg(s.largest_angular);
    result["charged_bytes"] = int64_t(s.quota.used(superpos::MemoryDomain::History));
    return result;
}

Error SuperposSnapshotInterpolator::remove(uint64_t p_entity) {
    const Error ready = _preflight();
    if (ready != OK) { return ready; }
    if (!state->find(p_entity)) { return ERR_DOES_NOT_EXIST; }
    state->erase(p_entity);
    return OK;
}

Error SuperposSnapshotInterpolator::clear() {
    const Error ready = _preflight();
    if (ready != OK) { return ready; }
    State &s = *state;
    for (auto &track : s.tracks.span()) { if (track.used) { s.release(track); } }
    for (auto &entry : s.table.span()) { entry = 0; }
    s.live = 0;
    s.started = false;
    s.clock_tick = s.newest_tick = 0;
    s.clock_fraction = s.since_arrival = s.cadence_peak = 0.0;
    s.delay = s.base_delay;
    s.interpolated = s.extrapolated = s.held = s.priming = s.gap_holds = s.corrections = s.teleports = 0;
    s.epoch_resets = s.rejected_epochs = s.rejected_stale = s.clock_holds = s.clock_resyncs = 0;
    s.largest_correction = s.largest_angular = 0.0;
    return OK;
}
