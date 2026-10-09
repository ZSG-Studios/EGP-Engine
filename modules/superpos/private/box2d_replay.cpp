#include "box2d_replay.hpp"
#include "module_memory.hpp"
#include "engine_prediction.hpp"
#include "superpos/codec.hpp"
#include "core/os/thread.h"
#include "modules/modules_enabled.gen.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <type_traits>

#ifdef MODULE_BOX2D_ENABLED
#include "modules/box2d/local_replay.h"
namespace superpos_egp {
namespace {
constexpr uint64_t local_magic = 0x3152504c32425053ULL;
template<class T> class Records {
    static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>);
    static_assert(alignof(T) <= alignof(std::max_align_t));
    superpos::Buffer backing;
    size_t count = 0;
public:
    explicit Records(superpos::Allocator &allocator) noexcept : backing(allocator, superpos::MemoryDomain::History) {}
    superpos::Status initialize(size_t size) noexcept {
        if (size > SIZE_MAX / sizeof(T)) return superpos::fail(superpos::Error::Overflow);
        if (auto result = backing.resize(size * sizeof(T)); !result) return result;
        count = size;
        // The engine records contain only bounded scalar/RID/vector data; no
        // callbacks, heap-owning fields or constructors that allocate.
        for (size_t i = 0; i < count; ++i) std::construct_at(data() + i);
        return {};
    }
    T *data() noexcept { return reinterpret_cast<T *>(backing.bytes().data()); }
    std::span<std::byte> bytes(size_t size) noexcept { return backing.bytes().first(size * sizeof(T)); }
};
void *native_allocate(void *context, size_t bytes, size_t alignment) noexcept {
    return static_cast<superpos::BudgetAllocator *>(context)->allocate(bytes, alignment, superpos::MemoryDomain::Backend);
}
void native_deallocate(void *context, void *memory) noexcept {
    static_cast<superpos::BudgetAllocator *>(context)->deallocate(memory);
}
}
struct Box2DReplayParticipant::Storage {
    Box2DPhysicsServer2D *server;
    RID space;
    Box2DReplayLimits limits;
    Box2DReplayQualification qualification;
    Records<Box2DLocalReplay::BodyRecord> bodies;
    Records<Box2DLocalReplay::ShapeRecord> shapes;
    Records<Box2DLocalReplay::ContactRecord> contacts;
    superpos::Buffer image_staging, image, canonical;
    Box2DLocalReplay::PreparedRestore prepared;
    bool busy = false;
    Storage(Box2DPhysicsServer2D *server, RID space, Box2DReplayLimits limits, Box2DReplayQualification qualification) noexcept
        : server(server), space(space), limits(limits), qualification(qualification),
          bodies(module_backing()), shapes(module_backing()), contacts(module_backing()),
          image_staging(module_backing(), superpos::MemoryDomain::History),
          image(module_backing(), superpos::MemoryDomain::History),
          canonical(module_backing(), superpos::MemoryDomain::History) {}
    bool current(superpos::Epoch &epoch, superpos::Tick &tick) noexcept {
        return Thread::is_main_thread() && qualification.current &&
            qualification.current(qualification.context, qualification.process_configuration, qualification.topology, epoch, tick) && epoch;
    }
};
superpos::Result<CapturedOwner<Box2DReplayParticipant>> Box2DReplayParticipant::create(
        Box2DPhysicsServer2D *server, RID space, Box2DReplayLimits limits, Box2DReplayQualification qualification) noexcept {
    if (!Thread::is_main_thread()) return superpos::fail(superpos::Error::PermissionDenied);
    if (!server || !qualification.participant_id || !qualification.schema_version || !qualification.current ||
        !superpos::known_fingerprint(qualification.simulation) || !superpos::known_fingerprint(qualification.process_configuration) ||
        !superpos::known_fingerprint(qualification.topology) ||
        uint64_t(limits.bodies) + limits.shapes > 4094 || limits.contacts > 16384 ||
        !limits.image_bytes || limits.image_bytes > (64u << 20) || !limits.native_bytes || limits.native_bytes > (64u << 20))
        return superpos::fail(superpos::Error::InvalidArgument);
    if (auto valid = module_memory_configuration(); !valid) return superpos::fail(valid.error());
    auto &allocator = module_backing();
    auto *memory = allocator.allocate(sizeof(Storage), alignof(Storage), superpos::MemoryDomain::Session);
    if (!memory) return superpos::fail(superpos::Error::OutOfMemory);
    StorageOwner storage(std::construct_at(static_cast<Storage *>(memory), server, space, limits, qualification));
    superpos::Epoch epoch = 0; superpos::Tick tick = 0;
    if (!storage->current(epoch, tick)) return superpos::fail(superpos::Error::NotReady);
    Box2DLocalReplay::Requirements required;
    auto measured = Box2DLocalReplay::measure(server, space, required);
    if (measured != OK) return superpos::fail(simulation_error(measured));
    if (required.bodies > limits.bodies || required.shape_records > limits.shapes || required.contacts > limits.contacts)
        return superpos::fail(superpos::Error::OutOfMemory);
    const size_t maximum = 256 + sizeof(Box2DLocalReplay::SpaceRecord) + size_t(limits.bodies) * sizeof(Box2DLocalReplay::BodyRecord) +
        size_t(limits.shapes) * sizeof(Box2DLocalReplay::ShapeRecord) + size_t(limits.contacts) * sizeof(Box2DLocalReplay::ContactRecord) + limits.image_bytes;
    auto allocated = storage->bodies.initialize(limits.bodies);
    if (allocated) allocated = storage->shapes.initialize(limits.shapes);
    if (allocated) allocated = storage->contacts.initialize(limits.contacts);
    if (allocated) allocated = storage->image_staging.resize(limits.image_bytes);
    if (allocated) allocated = storage->image.resize(limits.image_bytes);
    if (allocated) allocated = storage->canonical.resize(maximum);
    if (!allocated) return superpos::fail(allocated.error());
    superpos::Epoch after_epoch = 0; superpos::Tick after_tick = 0;
    if (!storage->current(after_epoch, after_tick) || after_epoch != epoch || after_tick != tick)
        return superpos::fail(superpos::Error::NotReady);
    superpos::RecoveryParticipantDescriptor descriptor{qualification.participant_id, qualification.schema_version, maximum,
        superpos::RecoveryGrade::LocalRestart, qualification.simulation, qualification.process_configuration};
    memory = allocator.allocate(sizeof(Box2DReplayParticipant), alignof(Box2DReplayParticipant), superpos::MemoryDomain::Session);
    if (!memory) return superpos::fail(superpos::Error::OutOfMemory);
    return CapturedOwner<Box2DReplayParticipant>(new(memory) Box2DReplayParticipant(std::move(storage), descriptor),
        CapturedDelete<Box2DReplayParticipant>{&allocator});
}
superpos::Result<size_t> Box2DReplayParticipant::capture(std::span<std::byte> output) const noexcept {
    auto &s = *storage;
    if (!Thread::is_main_thread()) return superpos::fail(superpos::Error::PermissionDenied);
    if (s.busy || s.prepared.active()) return superpos::fail(superpos::Error::Busy);
    s.busy = true;
    struct Release { bool &busy; ~Release() { busy = false; } } release{s.busy};
    superpos::Epoch epoch = 0; superpos::Tick tick = 0;
    if (!s.current(epoch, tick)) return superpos::fail(superpos::Error::NotReady);
    Box2DLocalReplay::SpaceRecord checkpoint;
    Box2DLocalReplay::Requirements written;
    auto result = Box2DLocalReplay::checkpoint(s.server, s.space, s.bodies.data(), s.limits.bodies,
        s.shapes.data(), s.limits.shapes, s.image_staging.bytes().data(), s.image_staging.size(),
        s.image.bytes().data(), s.image.size(), checkpoint, written, s.contacts.data(), s.limits.contacts);
    if (result != OK) return superpos::fail(simulation_error(result));
    superpos::Epoch after_epoch = 0; superpos::Tick after_tick = 0;
    if (!s.current(after_epoch, after_tick) || epoch != after_epoch || tick != after_tick)
        return superpos::fail(superpos::Error::NotReady);
    // Full native-layout checkpoint, never a position-only snapshot or wire
    // packet. Includes wrapper/config/contacts/force order and the solver image.
    superpos::Writer writer(s.canonical.bytes());
    if (!writer.u64(local_magic) || !writer.u64(epoch) || !writer.u64(tick) ||
        !writer.raw(frozen.simulation) || !writer.raw(frozen.local_configuration) || !writer.raw(s.qualification.topology) ||
        !writer.varuint(written.bodies) || !writer.varuint(written.shape_records) || !writer.varuint(written.contacts) ||
        !writer.varuint(checkpoint.image_bytes) ||
        !writer.raw({reinterpret_cast<const std::byte *>(&checkpoint), sizeof(checkpoint)}) ||
        !writer.raw(s.bodies.bytes(written.bodies)) || !writer.raw(s.shapes.bytes(written.shape_records)) ||
        !writer.raw(s.contacts.bytes(written.contacts)) || !writer.raw(s.image.bytes().first(checkpoint.image_bytes)))
        return superpos::fail(superpos::Error::Overflow);
    if (output.size() < writer.size()) return superpos::fail(superpos::Error::OutOfMemory);
    std::memmove(output.data(), s.canonical.bytes().data(), writer.size());
    return writer.size();
}
superpos::Status Box2DReplayParticipant::stage_restore(superpos::Epoch epoch, superpos::Tick tick, std::span<const std::byte> bytes) noexcept {
    auto &s = *storage;
    if (!Thread::is_main_thread()) return superpos::fail(superpos::Error::PermissionDenied);
    if (s.busy || s.prepared.active()) return superpos::fail(superpos::Error::Busy);
    if (bytes.empty() || bytes.size() > s.canonical.size()) return superpos::fail(superpos::Error::InvalidArgument);
    s.busy = true;
    struct Failure { Storage &s; bool success = false; ~Failure() { if (!success) { Box2DLocalReplay::abort_restore(s.prepared); s.busy = false; } } } failure{s};
    // Snapshot the complete caller-owned input before the first admission or
    // allocator callback. Prepared native binding inputs then remain owned.
    std::memmove(s.canonical.bytes().data(), bytes.data(), bytes.size());
    superpos::Reader reader(s.canonical.bytes().first(bytes.size()));
    auto magic = reader.u64(), encoded_epoch = reader.u64(), encoded_tick = reader.u64();
    auto simulation = reader.raw(32), configuration = reader.raw(32), topology = reader.raw(32);
    auto bodies = reader.varuint(), shapes = reader.varuint(), contacts = reader.varuint(), image_size = reader.varuint();
    if (!magic || !encoded_epoch || !encoded_tick || !simulation || !configuration || !topology || !bodies || !shapes || !contacts || !image_size ||
        *magic != local_magic || *encoded_epoch != epoch || *encoded_tick != tick ||
        !std::equal(simulation->begin(), simulation->end(), frozen.simulation.begin()) ||
        !std::equal(configuration->begin(), configuration->end(), frozen.local_configuration.begin()) ||
        !std::equal(topology->begin(), topology->end(), s.qualification.topology.begin()) ||
        *bodies > s.limits.bodies || *shapes > s.limits.shapes || *contacts > s.limits.contacts ||
        !*image_size || *image_size > s.limits.image_bytes)
        return superpos::fail(superpos::Error::IncompatibleSchema);
    auto space = reader.raw(sizeof(Box2DLocalReplay::SpaceRecord));
    auto body_data = reader.raw(size_t(*bodies) * sizeof(Box2DLocalReplay::BodyRecord));
    auto shape_data = reader.raw(size_t(*shapes) * sizeof(Box2DLocalReplay::ShapeRecord));
    auto contact_data = reader.raw(size_t(*contacts) * sizeof(Box2DLocalReplay::ContactRecord));
    auto image = reader.raw(size_t(*image_size));
    if (!space || !body_data || !shape_data || !contact_data || !image || !reader.empty()) return superpos::fail(superpos::Error::InvalidArgument);
    Box2DLocalReplay::SpaceRecord checkpoint;
    std::memcpy(&checkpoint, space->data(), sizeof(checkpoint));
    if (checkpoint.rid != s.space) return superpos::fail(superpos::Error::IncompatibleSchema);
    if (!body_data->empty()) std::memcpy(s.bodies.data(), body_data->data(), body_data->size());
    if (!shape_data->empty()) std::memcpy(s.shapes.data(), shape_data->data(), shape_data->size());
    if (!contact_data->empty()) std::memcpy(s.contacts.data(), contact_data->data(), contact_data->size());
    std::memcpy(s.image.bytes().data(), image->data(), image->size());
    superpos::Epoch current_epoch = 0; superpos::Tick current_tick = 0;
    if (!s.current(current_epoch, current_tick) || current_epoch != epoch) return superpos::fail(superpos::Error::NotReady);
    const spB2CandidateAllocator allocator{&module_backing(), native_allocate, native_deallocate};
    auto result = Box2DLocalReplay::prepare_restore(s.server, checkpoint, s.bodies.data(), uint32_t(*bodies),
        s.shapes.data(), uint32_t(*shapes), s.image.bytes().data(), image->size(), s.limits.native_bytes,
        s.prepared, allocator, s.contacts.data(), uint32_t(*contacts));
    if (result != OK) return superpos::fail(simulation_error(result));
    superpos::Epoch after_epoch = 0; superpos::Tick after_tick = 0;
    if (!s.current(after_epoch, after_tick) || after_epoch != current_epoch || after_tick != current_tick)
        return superpos::fail(superpos::Error::NotReady);
    failure.success = true;
    return {};
}
void Box2DReplayParticipant::commit_restore() noexcept { Box2DLocalReplay::commit_restore(storage->prepared); }
void Box2DReplayParticipant::abort_restore() noexcept { finish_restore(); }
void Box2DReplayParticipant::finish_restore() noexcept {
    if (!Thread::is_main_thread()) return; // No cross-thread state writes or mutex release.
    // Called only outside the multi-participant commit loop. The old arena's
    // deallocator may reenter; busy stays set until complete retirement.
    Box2DLocalReplay::abort_restore(storage->prepared);
    storage->busy = false;
}
} // namespace superpos_egp
#else
namespace superpos_egp {
struct Box2DReplayParticipant::Storage {};
superpos::Result<CapturedOwner<Box2DReplayParticipant>> Box2DReplayParticipant::create(Box2DPhysicsServer2D *, RID, Box2DReplayLimits, Box2DReplayQualification) noexcept { return superpos::fail(superpos::Error::Unsupported); }
superpos::Result<size_t> Box2DReplayParticipant::capture(std::span<std::byte>) const noexcept { return superpos::fail(superpos::Error::Unsupported); }
superpos::Status Box2DReplayParticipant::stage_restore(superpos::Epoch, superpos::Tick, std::span<const std::byte>) noexcept { return superpos::fail(superpos::Error::Unsupported); }
void Box2DReplayParticipant::commit_restore() noexcept {}
void Box2DReplayParticipant::abort_restore() noexcept {}
void Box2DReplayParticipant::finish_restore() noexcept {}
} // namespace superpos_egp
#endif
namespace superpos_egp {
void Box2DReplayParticipant::StorageDelete::operator()(Storage *value) const noexcept {
    if (value) { std::destroy_at(value); module_backing().deallocate(value); }
}
Box2DReplayParticipant::Box2DReplayParticipant(StorageOwner value, superpos::RecoveryParticipantDescriptor descriptor) noexcept
    : storage(std::move(value)), frozen(descriptor) {}
Box2DReplayParticipant::~Box2DReplayParticipant() {
    CRASH_COND(!Thread::is_main_thread()); // Borrowed Space and its barrier retire on their owner.
    if (storage) finish_restore();
}
superpos::Status restore_box2d_participants(superpos::Epoch epoch, superpos::Tick tick,
        std::span<const Box2DReplayRestore> rows, const superpos::Fingerprint &configuration) noexcept {
    if (!Thread::is_main_thread()) return superpos::fail(superpos::Error::PermissionDenied);
    if (rows.empty() || rows.size() > 2 || !superpos::known_fingerprint(configuration))
        return superpos::fail(superpos::Error::InvalidArgument);
    std::array<Box2DReplayRestore, 2> frozen;
    std::copy(rows.begin(), rows.end(), frozen.begin());
    std::array<superpos::RecoveryPart, 2> parts;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (!frozen[i].participant || frozen[i].checkpoint.empty()) return superpos::fail(superpos::Error::InvalidArgument);
        for (size_t j = 0; j < i; ++j)
            if (frozen[j].participant == frozen[i].participant) return superpos::fail(superpos::Error::InvalidArgument);
        const auto descriptor = frozen[i].participant->descriptor();
        parts[i] = {frozen[i].participant, descriptor.schema_version, frozen[i].checkpoint, descriptor.simulation, descriptor.local_configuration};
    }
    const auto result = superpos::restore_participants(epoch, tick, std::span(parts).first(rows.size()),
        {superpos::RecoveryGrade::LocalRestart, configuration});
    for (size_t i = 0; i < rows.size(); ++i) frozen[i].participant->finish_restore();
    return result;
}
} // namespace superpos_egp
