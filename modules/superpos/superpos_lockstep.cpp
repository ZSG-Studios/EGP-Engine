// SPDX-License-Identifier: MIT
#include "superpos_lockstep.h"
#include "superpos_session.h"
#include "u64_bits.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "superpos/lockstep.hpp"
#include <array>
#include <memory>
#include <new>
#include <optional>

namespace {
Error lockstep_error(superpos::Error value) {
    switch (value) {
        case superpos::Error::None: return OK;
        case superpos::Error::CapacityExceeded: case superpos::Error::OutOfMemory: return ERR_OUT_OF_MEMORY;
        case superpos::Error::NotReady: return ERR_UNCONFIGURED;
        case superpos::Error::Busy: return ERR_BUSY;
        case superpos::Error::Unsupported: return ERR_UNAVAILABLE;
        // History no longer covers the requested ticks: the client needs a keyframe.
        case superpos::Error::StaleEpoch: return ERR_DOES_NOT_EXIST;
        case superpos::Error::InvalidArgument: return ERR_INVALID_PARAMETER;
        default: return ERR_INVALID_DATA;
    }
}
std::span<const std::byte> bytes_of(const PackedByteArray &p_bytes) {
    return {reinterpret_cast<const std::byte *>(p_bytes.ptr()), size_t(p_bytes.size())};
}
PackedByteArray packed(std::span<const std::byte> p_bytes) {
    PackedByteArray result;
    if (result.resize(int64_t(p_bytes.size())) == OK && !p_bytes.empty()) {
        memcpy(result.ptrw(), p_bytes.data(), p_bytes.size());
    }
    return result;
}
constexpr int64_t wire_ceiling = 4096;
} // namespace

struct SuperposLockstepClient::Impl {
    // Declared first so it outlives the decoder storage it backs.
    superpos::BudgetAllocator allocator{superpos::MemoryPlan::client()};
    std::optional<superpos::InputHistory> history;
    std::optional<superpos::CommandDecoder> commands;
    int64_t slots = 0;
};

SuperposLockstepClient::SuperposLockstepClient() { impl = memnew(Impl); }
SuperposLockstepClient::~SuperposLockstepClient() { memdelete(impl); }

Error SuperposLockstepClient::configure(int64_t p_input_bytes, int64_t p_slots, int64_t p_buffer_ticks) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (p_input_bytes < 1 || p_input_bytes > int64_t(superpos::lockstep_input_bytes) || p_slots < 1 || p_slots > int64_t(superpos::lockstep_slots)) { return ERR_INVALID_PARAMETER; }
    if (p_buffer_ticks < 1 || p_buffer_ticks > int64_t(superpos::lockstep_max_command_history)) { return ERR_INVALID_PARAMETER; }
    impl->commands.reset();
    auto history = superpos::InputHistory::create(size_t(p_input_bytes));
    auto commands = superpos::CommandDecoder::create(impl->allocator, { size_t(p_input_bytes), size_t(p_slots), size_t(p_buffer_ticks) });
    if (!history) { return ERR_INVALID_PARAMETER; }
    if (!commands) { return lockstep_error(commands.error()); }
    impl->history.emplace(std::move(*history));
    impl->commands.emplace(std::move(*commands));
    impl->slots = p_slots;
    return OK;
}
Error SuperposLockstepClient::record_input(int64_t p_tick, const PackedByteArray &p_input) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (!impl->history) { return ERR_UNCONFIGURED; }
    if (p_tick <= 0) { return ERR_INVALID_PARAMETER; }
    auto recorded = impl->history->record(superpos::Tick(p_tick), bytes_of(p_input));
    return recorded ? OK : lockstep_error(recorded.error());
}
Error SuperposLockstepClient::acknowledge_inputs(int64_t p_tick) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (!impl->history) { return ERR_UNCONFIGURED; }
    if (p_tick < 0) { return ERR_INVALID_PARAMETER; }
    auto acknowledged = impl->history->acknowledge(superpos::Tick(p_tick));
    return acknowledged ? OK : lockstep_error(acknowledged.error());
}
int64_t SuperposLockstepClient::discard_oldest_inputs(int64_t p_count) {
    if (Thread::get_caller_id() != owner_thread || !impl->history || p_count < 0) { return 0; }
    return int64_t(impl->history->discard_oldest(size_t(p_count)));
}
PackedByteArray SuperposLockstepClient::pack_inputs(int64_t p_max_ticks, int64_t p_max_bytes) const {
    if (Thread::get_caller_id() != owner_thread || !impl->history || p_max_ticks < 0 || p_max_bytes < 8 || p_max_bytes > wire_ceiling) { return PackedByteArray(); }
    std::array<std::byte, wire_ceiling> buffer{};
    // Acknowledge the newest command tick received in order (not merely applied), so the
    // server never resends ticks already buffered here.
    // The held tick (parked batches beyond a gap included) lets the server repair a
    // lost command batch at once instead of after its stall timer.
    auto encoded = impl->history->encode(impl->commands->newest(), size_t(p_max_ticks), std::span(buffer).first(size_t(p_max_bytes)), impl->commands->held());
    return encoded ? packed(std::span<const std::byte>(buffer).first(*encoded)) : PackedByteArray();
}
Error SuperposLockstepClient::accept_commands(const PackedByteArray &p_payload) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (!impl->commands) { return ERR_UNCONFIGURED; }
    auto accepted = impl->commands->accept(bytes_of(p_payload));
    return accepted ? OK : lockstep_error(accepted.error());
}
Error SuperposLockstepClient::load_keyframe(const PackedByteArray &p_table) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (!impl->commands) { return ERR_UNCONFIGURED; }
    auto loaded = impl->commands->load_table(bytes_of(p_table));
    return loaded ? OK : lockstep_error(loaded.error());
}
int64_t SuperposLockstepClient::advance_command() {
    if (Thread::get_caller_id() != owner_thread || !impl->commands) { return 0; }
    auto tick = impl->commands->advance();
    return tick ? superpos_egp::signed_bits(*tick) : 0;
}
Array SuperposLockstepClient::get_inputs() const {
    Array result;
    if (Thread::get_caller_id() != owner_thread || !impl->commands) { return result; }
    result.resize(impl->slots);
    for (int64_t slot = 0; slot < impl->slots; ++slot) { result[slot] = packed(impl->commands->input(uint16_t(slot))); }
    return result;
}
int64_t SuperposLockstepClient::get_processed_tick() const {
    if (Thread::get_caller_id() != owner_thread || !impl->commands) { return 0; }
    return superpos_egp::signed_bits(impl->commands->processed());
}
PackedByteArray SuperposLockstepClient::get_input(int64_t p_slot) const {
    if (Thread::get_caller_id() != owner_thread || !impl->commands || p_slot < 0 || p_slot >= impl->slots) { return PackedByteArray(); }
    return packed(impl->commands->input(uint16_t(p_slot)));
}
Dictionary SuperposLockstepClient::get_status() const {
    Dictionary result;
    result["configured"] = bool(impl->history);
    if (Thread::get_caller_id() != owner_thread || !impl->history) { return result; }
    result["recorded_tick"] = superpos_egp::signed_bits(impl->history->newest());
    result["unacknowledged_inputs"] = int64_t(impl->history->pending());
    result["applied_command_tick"] = superpos_egp::signed_bits(impl->commands->applied());
    result["newest_command_tick"] = superpos_egp::signed_bits(impl->commands->newest());
    result["buffered_commands"] = int64_t(impl->commands->buffered());
    result["processed_tick"] = superpos_egp::signed_bits(impl->commands->processed());
    result["deferred_commands"] = int64_t(impl->commands->deferred());
    return result;
}

void SuperposLockstepClient::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "input_bytes", "slots", "buffer_ticks"), &SuperposLockstepClient::configure, DEFVAL(128));
    ClassDB::bind_method(D_METHOD("record_input", "tick", "input"), &SuperposLockstepClient::record_input);
    ClassDB::bind_method(D_METHOD("acknowledge_inputs", "tick"), &SuperposLockstepClient::acknowledge_inputs);
    ClassDB::bind_method(D_METHOD("discard_oldest_inputs", "count"), &SuperposLockstepClient::discard_oldest_inputs);
    ClassDB::bind_method(D_METHOD("pack_inputs", "max_ticks", "max_bytes"), &SuperposLockstepClient::pack_inputs, DEFVAL(60), DEFVAL(880));
    ClassDB::bind_method(D_METHOD("accept_commands", "payload"), &SuperposLockstepClient::accept_commands);
    ClassDB::bind_method(D_METHOD("load_keyframe", "table"), &SuperposLockstepClient::load_keyframe);
    ClassDB::bind_method(D_METHOD("advance_command"), &SuperposLockstepClient::advance_command);
    ClassDB::bind_method(D_METHOD("get_input", "slot"), &SuperposLockstepClient::get_input);
    ClassDB::bind_method(D_METHOD("get_inputs"), &SuperposLockstepClient::get_inputs);
    ClassDB::bind_method(D_METHOD("get_processed_tick"), &SuperposLockstepClient::get_processed_tick);
    ClassDB::bind_method(D_METHOD("get_status"), &SuperposLockstepClient::get_status);
}

struct SuperposLockstepServer::Impl {
    // Declared first so it outlives the command history it backs.
    superpos::BudgetAllocator allocator;
    superpos::PlayoutConfig playout;
    std::array<std::unique_ptr<superpos::InputPlayout>, superpos::lockstep_slots> peers;
    std::optional<superpos::CommandEncoder> commands;
    superpos::CommandStreamPolicy stream_policy;
    std::array<superpos::CommandStream, superpos::lockstep_slots> streams;
    // Batch service state per slot: the bound session, its outstanding
    // unreliable command sends (retired once the carrier took them) and its
    // starvation hold input.
    struct Link {
        Ref<SuperposSession> session;
        bool enabled = false, held = false;
        uint32_t interval = 1;
        std::array<uint64_t, 8> outstanding{};
        std::array<uint64_t, 8> outstanding_at{};
        size_t outstanding_count = 0;
        uint64_t processed = 0, bytes = 0, skipped = 0;
        std::array<uint8_t, superpos::lockstep_input_bytes> hold{};
    };
    std::array<Link, superpos::lockstep_slots> links;
    int64_t slots = 0;
};

SuperposLockstepServer::SuperposLockstepServer() { impl = memnew(Impl); }
SuperposLockstepServer::~SuperposLockstepServer() { memdelete(impl); }

Error SuperposLockstepServer::configure(int64_t p_input_bytes, int64_t p_slots, const Dictionary &p_playout) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (p_input_bytes < 1 || p_input_bytes > int64_t(superpos::lockstep_input_bytes) || p_slots < 1 || p_slots > int64_t(superpos::lockstep_slots)) { return ERR_INVALID_PARAMETER; }
    for (const Variant &key : p_playout.keys()) {
        const String name = key;
        if (name != "initial_target" && name != "minimum_target" && name != "maximum_target" && name != "relax_ticks" && name != "catch_up_margin" && name != "command_history_ticks" && name != "command_change_capacity" && name != "stream_redundant_ticks" && name != "stream_catch_up_ticks" && name != "stream_resync_backlog_bytes" && name != "stream_resync_holdoff_ms") { return ERR_INVALID_PARAMETER; }
    }
    superpos::PlayoutConfig config;
    config.input_bytes = size_t(p_input_bytes);
    config.initial_target = size_t(int64_t(p_playout.get("initial_target", int64_t(config.initial_target))));
    config.minimum_target = size_t(int64_t(p_playout.get("minimum_target", int64_t(config.minimum_target))));
    config.maximum_target = size_t(int64_t(p_playout.get("maximum_target", int64_t(config.maximum_target))));
    config.relax_ticks = superpos::Tick(int64_t(p_playout.get("relax_ticks", int64_t(config.relax_ticks))));
    config.catch_up_margin = size_t(int64_t(p_playout.get("catch_up_margin", int64_t(config.catch_up_margin))));
    if (!superpos::InputPlayout::create(config)) { return ERR_INVALID_PARAMETER; }
    // The command history is the window in which a slow joiner or a stalled client
    // still catches up without another keyframe.
    const int64_t history_ticks = int64_t(p_playout.get("command_history_ticks", int64_t(superpos::lockstep_history_ticks)));
    const int64_t change_capacity = int64_t(p_playout.get("command_change_capacity", int64_t(0)));
    if (history_ticks < 1 || history_ticks > int64_t(superpos::lockstep_max_command_history) || change_capacity < 0) { return ERR_INVALID_PARAMETER; }
    impl->commands.reset();
    auto commands = superpos::CommandEncoder::create(impl->allocator, { size_t(p_input_bytes), size_t(p_slots), size_t(history_ticks), size_t(change_capacity) });
    if (!commands) { return lockstep_error(commands.error()); }
    const int64_t redundant = int64_t(p_playout.get("stream_redundant_ticks", int64_t(1)));
    const int64_t catch_up = int64_t(p_playout.get("stream_catch_up_ticks", int64_t(30)));
    // A client whose unacknowledged backlog exceeds this many encoded bytes is
    // reported stale (resync by keyframe) instead of replaying it; 0 disables.
    const int64_t resync_backlog = int64_t(p_playout.get("stream_resync_backlog_bytes", int64_t(0)));
    const int64_t resync_holdoff = int64_t(p_playout.get("stream_resync_holdoff_ms", int64_t(30000)));
    if (redundant < 0 || redundant > 16 || catch_up < 0 || resync_backlog < 0 || resync_holdoff < 0 || resync_holdoff > 3600000) { return ERR_INVALID_PARAMETER; }
    impl->stream_policy.redundant_ticks = size_t(redundant);
    impl->stream_policy.catch_up_ticks = superpos::Tick(catch_up);
    impl->stream_policy.resync_backlog_bytes = uint64_t(resync_backlog);
    impl->stream_policy.resync_holdoff_us = uint64_t(resync_holdoff) * 1000;
    for (auto &stream : impl->streams) { stream = superpos::CommandStream(impl->stream_policy); }
    impl->playout = config;
    impl->commands.emplace(std::move(*commands));
    for (auto &peer : impl->peers) { peer.reset(); }
    impl->slots = p_slots;
    return OK;
}
Error SuperposLockstepServer::accept_inputs(int64_t p_slot, const PackedByteArray &p_payload) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (!impl->commands) { return ERR_UNCONFIGURED; }
    if (p_slot < 0 || p_slot >= impl->slots) { return ERR_INVALID_PARAMETER; }
    auto &peer = impl->peers[size_t(p_slot)];
    if (!peer) {
        auto created = superpos::InputPlayout::create(impl->playout);
        if (!created) { return lockstep_error(created.error()); }
        peer.reset(new (std::nothrow) superpos::InputPlayout(std::move(*created)));
        if (!peer) { return ERR_OUT_OF_MEMORY; }
    }
    auto accepted = peer->accept(bytes_of(p_payload));
    return accepted ? OK : lockstep_error(accepted.error());
}
Array SuperposLockstepServer::consume_inputs(int64_t p_slot) {
    Array result;
    if (Thread::get_caller_id() != owner_thread || !impl->commands || p_slot < 0 || p_slot >= impl->slots || !impl->peers[size_t(p_slot)]) { return result; }
    std::array<superpos::PlayoutInput, 2> out{};
    auto consumed = impl->peers[size_t(p_slot)]->consume(out);
    if (!consumed) { return result; }
    for (size_t i = 0; i < *consumed; ++i) {
        Dictionary item;
        item["tick"] = superpos_egp::signed_bits(out[i].tick);
        item["input"] = packed(out[i].bytes);
        result.push_back(item);
    }
    return result;
}
Dictionary SuperposLockstepServer::get_playout_status(int64_t p_slot) const {
    Dictionary result;
    result["active"] = false;
    if (Thread::get_caller_id() != owner_thread || !impl->commands || p_slot < 0 || p_slot >= impl->slots || !impl->peers[size_t(p_slot)]) { return result; }
    const auto &peer = *impl->peers[size_t(p_slot)];
    const auto status = peer.status();
    result["active"] = true;
    result["received_tick"] = superpos_egp::signed_bits(status.received);
    result["consumed_tick"] = superpos_egp::signed_bits(status.consumed);
    result["command_acknowledged"] = superpos_egp::signed_bits(status.command_acknowledged);
    result["buffered"] = int64_t(status.buffered);
    result["target"] = int64_t(status.target);
    result["starvations"] = int64_t(status.starvations);
    result["duplicates"] = int64_t(status.duplicates);
    result["late"] = int64_t(status.late);
    result["skipped"] = int64_t(status.skipped);
    result["playing"] = status.playing;
    result["pace_advice"] = peer.pace_advice();
    return result;
}
Error SuperposLockstepServer::begin_tick(int64_t p_tick) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (!impl->commands) { return ERR_UNCONFIGURED; }
    if (p_tick <= 0) { return ERR_INVALID_PARAMETER; }
    auto begun = impl->commands->begin(superpos::Tick(p_tick));
    return begun ? OK : lockstep_error(begun.error());
}
Error SuperposLockstepServer::set_command(int64_t p_slot, const PackedByteArray &p_input, int64_t p_processed_tick) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (!impl->commands) { return ERR_UNCONFIGURED; }
    if (p_slot < 0 || p_slot >= impl->slots) { return ERR_INVALID_PARAMETER; }
    if (p_processed_tick < 0) { return ERR_INVALID_PARAMETER; }
    auto set = impl->commands->set(uint16_t(p_slot), bytes_of(p_input), superpos::Tick(p_processed_tick));
    return set ? OK : lockstep_error(set.error());
}
Error SuperposLockstepServer::end_tick() {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (!impl->commands) { return ERR_UNCONFIGURED; }
    auto ended = impl->commands->end();
    return ended ? OK : lockstep_error(ended.error());
}
Dictionary SuperposLockstepServer::pack_commands(int64_t p_after_tick, int64_t p_max_bytes, int64_t p_recipient) const {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    result["payload"] = PackedByteArray();
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (!impl->commands) { return result; }
    if (p_after_tick < 0 || p_max_bytes < 8 || p_max_bytes > wire_ceiling || p_recipient < -1 || p_recipient >= impl->slots) { result["error"] = ERR_INVALID_PARAMETER; return result; }
    std::array<std::byte, wire_ceiling> buffer{};
    const uint32_t recipient = p_recipient < 0 ? superpos::lockstep_no_recipient : uint32_t(p_recipient);
    auto encoded = impl->commands->encode(superpos::Tick(p_after_tick), std::span(buffer).first(size_t(p_max_bytes)), recipient);
    if (!encoded) { result["error"] = lockstep_error(encoded.error()); return result; }
    result["error"] = OK;
    result["payload"] = packed(std::span<const std::byte>(buffer).first(*encoded));
    return result;
}
Dictionary SuperposLockstepServer::pack_command_window(int64_t p_acknowledged_tick, int64_t p_sent_tick, int64_t p_max_bytes, int64_t p_recipient, int64_t p_redundant_ticks) const {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    result["payload"] = PackedByteArray();
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (!impl->commands) { return result; }
    if (p_acknowledged_tick < 0 || p_sent_tick < 0 || p_max_bytes < 8 || p_max_bytes > wire_ceiling || p_recipient < -1 || p_recipient >= impl->slots || p_redundant_ticks < 0) { result["error"] = ERR_INVALID_PARAMETER; return result; }
    std::array<std::byte, wire_ceiling> buffer{};
    const uint32_t recipient = p_recipient < 0 ? superpos::lockstep_no_recipient : uint32_t(p_recipient);
    auto encoded = impl->commands->encode_window(superpos::Tick(p_acknowledged_tick), superpos::Tick(p_sent_tick), std::span(buffer).first(size_t(p_max_bytes)), recipient, size_t(p_redundant_ticks));
    if (!encoded) { result["error"] = lockstep_error(encoded.error()); return result; }
    result["error"] = OK;
    result["payload"] = packed(std::span<const std::byte>(buffer).first(*encoded));
    return result;
}
void SuperposLockstepServer::reset_stream(int64_t p_slot, int64_t p_keyframe_tick) {
    if (Thread::get_caller_id() != owner_thread || p_slot < 0 || p_slot >= impl->slots || p_keyframe_tick < 0) { return; }
    impl->streams[size_t(p_slot)].reset(superpos::Tick(p_keyframe_tick), OS::get_singleton()->get_ticks_usec());
}
Dictionary SuperposLockstepServer::pack_stream(int64_t p_slot, int64_t p_srtt_usec, int64_t p_max_bytes) {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    result["payload"] = PackedByteArray();
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (!impl->commands) { return result; }
    if (p_slot < 0 || p_slot >= impl->slots || p_srtt_usec < 0 || p_max_bytes < 8 || p_max_bytes > wire_ceiling) { result["error"] = ERR_INVALID_PARAMETER; return result; }
    // The recipient's newest in-order command tick rides on its input batches.
    const auto &peer = impl->peers[size_t(p_slot)];
    const auto playout = peer ? peer->status() : superpos::PlayoutStatus{};
    std::array<std::byte, wire_ceiling> buffer{};
    auto encoded = impl->streams[size_t(p_slot)].next(*impl->commands, playout.command_acknowledged, OS::get_singleton()->get_ticks_usec(), uint64_t(p_srtt_usec),
            std::span(buffer).first(size_t(p_max_bytes)), uint32_t(p_slot), playout.command_held);
    if (!encoded) { result["error"] = lockstep_error(encoded.error()); return result; }
    result["error"] = OK;
    result["payload"] = packed(std::span<const std::byte>(buffer).first(*encoded));
    return result;
}
Dictionary SuperposLockstepServer::get_stream_status(int64_t p_slot) const {
    Dictionary result;
    if (Thread::get_caller_id() != owner_thread || p_slot < 0 || p_slot >= impl->slots) { return result; }
    const auto status = impl->streams[size_t(p_slot)].status();
    result["acknowledged_tick"] = superpos_egp::signed_bits(status.acknowledged);
    result["sent_tick"] = superpos_egp::signed_bits(status.sent);
    result["batches"] = int64_t(status.batches);
    result["rewinds"] = int64_t(status.rewinds);
    result["repairs"] = int64_t(status.repairs);
    result["redundancy"] = int64_t(status.redundancy);
    result["resyncs"] = int64_t(status.resyncs);
    result["bytes"] = int64_t(impl->links[size_t(p_slot)].bytes);
    result["skipped"] = int64_t(impl->links[size_t(p_slot)].skipped);
    result["enabled"] = impl->links[size_t(p_slot)].enabled;
    return result;
}
void SuperposLockstepServer::bind_session(int64_t p_slot, const Ref<SuperposSession> &p_session) {
    if (Thread::get_caller_id() != owner_thread || p_slot < 0 || p_slot >= int64_t(superpos::lockstep_slots)) { return; }
    auto &link = impl->links[size_t(p_slot)];
    link.session = p_session;
    link.enabled = false;
    link.outstanding_count = 0;
}
void SuperposLockstepServer::set_stream_interval(int64_t p_slot, int64_t p_ticks) {
    if (Thread::get_caller_id() != owner_thread || p_slot < 0 || p_slot >= int64_t(superpos::lockstep_slots) || p_ticks < 1 || p_ticks > 60) { return; }
    impl->links[size_t(p_slot)].interval = uint32_t(p_ticks);
}
void SuperposLockstepServer::set_stream_enabled(int64_t p_slot, bool p_enabled) {
    if (Thread::get_caller_id() != owner_thread || p_slot < 0 || p_slot >= int64_t(superpos::lockstep_slots)) { return; }
    impl->links[size_t(p_slot)].enabled = p_enabled;
}
int64_t SuperposLockstepServer::ingest_inputs(int64_t p_channel, int64_t p_max_per_session) {
    if (Thread::get_caller_id() != owner_thread || !impl->commands || p_channel < 0 || p_channel >= 32 || p_max_per_session < 1) { return 0; }
    int64_t accepted = 0;
    PackedByteArray payload;
    for (int64_t slot = 0; slot < impl->slots; ++slot) {
        auto &link = impl->links[size_t(slot)];
        if (link.session.is_null() || !link.session->is_network_ready()) { continue; }
        for (int64_t n = 0; n < p_max_per_session; ++n) {
            uint64_t message = 0;
            if (link.session->read_raw(uint32_t(p_channel), payload, message) != OK) { break; }
            if (accept_inputs(slot, payload) == OK) { ++accepted; }
            // Busy: receipts are backed up; the message stays at the lane head.
            if (link.session->acknowledge_raw(message, uint32_t(p_channel)) != OK) { break; }
        }
    }
    return accepted;
}
Array SuperposLockstepServer::step_commands(int64_t p_tick, const PackedByteArray &p_hold_mask, const PackedByteArray &p_merge_mask) {
    Array result;
    if (Thread::get_caller_id() != owner_thread || !impl->commands || p_tick <= 0) { return result; }
    const size_t width = size_t(impl->playout.input_bytes);
    if (size_t(p_hold_mask.size()) != width || size_t(p_merge_mask.size()) != width) { return result; }
    if (!impl->commands->begin(superpos::Tick(p_tick))) { return result; }
    result.resize(impl->slots);
    std::array<superpos::PlayoutInput, 2> out{};
    std::array<uint8_t, superpos::lockstep_input_bytes> input{};
    for (int64_t slot = 0; slot < impl->slots; ++slot) {
        auto &link = impl->links[size_t(slot)];
        auto &peer = impl->peers[size_t(slot)];
        size_t produced = 0;
        if (peer) {
            auto consumed = peer->consume(out);
            produced = consumed ? *consumed : 0;
        }
        if (produced) {
            // Newest consumed input; one-shot bits of every consumed input survive a
            // catch-up merge. A starved tick later holds only the persistent bits.
            memcpy(input.data(), out[produced - 1].bytes.data(), width);
            for (size_t k = 0; k + 1 < produced; ++k) {
                for (size_t b = 0; b < width; ++b) { input[b] |= uint8_t(std::to_integer<uint8_t>(out[k].bytes[b]) & p_merge_mask[b]); }
            }
            link.processed = out[produced - 1].tick;
            for (size_t b = 0; b < width; ++b) { link.hold[b] = uint8_t(input[b] & p_hold_mask[b]); }
            link.held = true;
        } else if (link.held) {
            memcpy(input.data(), link.hold.data(), width);
        } else {
            input.fill(0);
        }
        (void)impl->commands->set(uint16_t(slot), std::span<const std::byte>(reinterpret_cast<const std::byte *>(input.data()), width), link.processed);
        PackedByteArray bytes;
        bytes.resize(int64_t(width));
        memcpy(bytes.ptrw(), input.data(), width);
        result[slot] = bytes;
    }
    if (!impl->commands->end()) { result.clear(); }
    return result;
}
Dictionary SuperposLockstepServer::publish_commands(int64_t p_channel, int64_t p_server_tick, int64_t p_max_bytes, int64_t p_max_waiting) {
    Dictionary result;
    PackedInt64Array stale;
    int64_t sent = 0, waiting = 0;
    if (Thread::get_caller_id() != owner_thread || !impl->commands || p_channel < 0 || p_channel >= 32 || p_max_bytes < 16 || p_max_bytes > wire_ceiling || p_max_waiting < 1) {
        result["sent"] = sent; result["stale"] = stale; return result;
    }
    const uint64_t now = OS::get_singleton()->get_ticks_usec();
    std::array<std::byte, wire_ceiling> buffer{};
    for (int64_t slot = 0; slot < impl->slots; ++slot) {
        auto &link = impl->links[size_t(slot)];
        if (!link.enabled || link.session.is_null()) { continue; }
        // Each slot publishes on its own cadence, phased by slot so batches spread
        // across ticks; every batch carries all ticks since the last one.
        if (link.interval > 1 && (uint64_t(p_server_tick) + uint64_t(slot)) % link.interval != 0) { continue; }
        if (!link.session->is_network_ready()) { continue; }
        // Retire sends the carrier already took; forget any older than 30 s (the
        // core expired them). Recent untaken sends are backpressure.
        size_t kept = 0, recent = 0;
        for (size_t i = 0; i < link.outstanding_count; ++i) {
            if (link.session->retire_raw(link.outstanding[i], uint32_t(p_channel)) == OK || now - link.outstanding_at[i] > 30000000) { continue; }
            link.outstanding[kept] = link.outstanding[i];
            link.outstanding_at[kept] = link.outstanding_at[i];
            recent += now - link.outstanding_at[i] < 1000000;
            ++kept;
        }
        link.outstanding_count = kept;
        if (recent >= size_t(p_max_waiting) || kept == link.outstanding.size()) { ++waiting; continue; }
        const auto &peer = impl->peers[size_t(slot)];
        const auto status = peer ? peer->status() : superpos::PlayoutStatus{};
        auto encoded = impl->streams[size_t(slot)].next(*impl->commands, status.command_acknowledged, now, link.session->smoothed_rtt_usec(),
                std::span(buffer).subspan(8, size_t(p_max_bytes) - 8), uint32_t(slot), status.command_held);
        if (!encoded) {
            if (encoded.error() == superpos::Error::StaleEpoch) { stale.push_back(slot); link.enabled = false; }
            continue;
        }
        // Header: the newest input tick received from this client (it retires
        // its input history up to there) and the server tick.
        const uint32_t received = uint32_t(status.received), tick = uint32_t(p_server_tick);
        memcpy(buffer.data(), &received, 4);
        memcpy(buffer.data() + 4, &tick, 4);
        uint64_t message = 0;
        if (link.session->enqueue_raw(reinterpret_cast<const uint8_t *>(buffer.data()), 8 + *encoded, uint32_t(p_channel), message) == OK) {
            link.outstanding[link.outstanding_count] = message;
            link.outstanding_at[link.outstanding_count] = now;
            ++link.outstanding_count;
            link.bytes += 8 + *encoded;
            ++sent;
        } else {
            ++link.skipped;
        }
    }
    result["sent"] = sent;
    result["waiting"] = waiting;
    result["stale"] = stale;
    return result;
}
Dictionary SuperposLockstepServer::get_streams_summary() const {
    // Every slot in one call, for live debugging views and telemetry.
    Dictionary result;
    if (Thread::get_caller_id() != owner_thread || !impl->commands) { return result; }
    PackedInt64Array acknowledged, sent, bytes, rewinds, repairs, skipped, received, buffered, starvations, target;
    PackedByteArray enabled, redundancy;
    const int64_t n = impl->slots;
    acknowledged.resize(n); sent.resize(n); bytes.resize(n); rewinds.resize(n); repairs.resize(n); redundancy.resize(n); skipped.resize(n);
    received.resize(n); buffered.resize(n); starvations.resize(n); target.resize(n); enabled.resize(n);
    for (int64_t slot = 0; slot < n; ++slot) {
        const auto stream = impl->streams[size_t(slot)].status();
        const auto &link = impl->links[size_t(slot)];
        const auto &peer = impl->peers[size_t(slot)];
        const auto playout = peer ? peer->status() : superpos::PlayoutStatus{};
        acknowledged.set(slot, superpos_egp::signed_bits(stream.acknowledged));
        sent.set(slot, superpos_egp::signed_bits(stream.sent));
        bytes.set(slot, int64_t(link.bytes));
        rewinds.set(slot, int64_t(stream.rewinds));
        repairs.set(slot, int64_t(stream.repairs));
        redundancy.set(slot, uint8_t(stream.redundancy));
        skipped.set(slot, int64_t(link.skipped));
        received.set(slot, superpos_egp::signed_bits(playout.received));
        buffered.set(slot, int64_t(playout.buffered));
        starvations.set(slot, int64_t(playout.starvations));
        target.set(slot, int64_t(playout.target));
        enabled.set(slot, link.enabled ? 1 : 0);
    }
    result["tick"] = superpos_egp::signed_bits(impl->commands->newest());
    result["acknowledged"] = acknowledged;
    result["sent"] = sent;
    result["bytes"] = bytes;
    result["rewinds"] = rewinds;
    result["repairs"] = repairs;
    result["redundancy"] = redundancy;
    result["skipped"] = skipped;
    result["input_received"] = received;
    result["input_buffered"] = buffered;
    result["input_starvations"] = starvations;
    result["input_target"] = target;
    result["enabled"] = enabled;
    return result;
}
PackedByteArray SuperposLockstepServer::pack_keyframe() const {
    if (Thread::get_caller_id() != owner_thread || !impl->commands) { return PackedByteArray(); }
    // A full table (every slot at the widest input) plus its header.
    std::array<std::byte, superpos::lockstep_slots * superpos::lockstep_input_bytes + 32> buffer{};
    auto encoded = impl->commands->encode_table(buffer);
    return encoded ? packed(std::span<const std::byte>(buffer).first(*encoded)) : PackedByteArray();
}
int64_t SuperposLockstepServer::get_command_tick() const {
    if (Thread::get_caller_id() != owner_thread || !impl->commands) { return 0; }
    return superpos_egp::signed_bits(impl->commands->newest());
}
Dictionary SuperposLockstepServer::get_command_totals() const {
    Dictionary result;
    if (Thread::get_caller_id() != owner_thread || !impl->commands) { return result; }
    const auto totals = impl->commands->totals();
    result["ticks"] = int64_t(totals.ticks);
    result["changes"] = int64_t(totals.changes);
    result["changed_bytes"] = int64_t(totals.changed_bytes);
    result["encoded_bytes"] = int64_t(totals.encoded_bytes);
    return result;
}
int64_t SuperposLockstepServer::get_oldest_command_tick() const {
    if (Thread::get_caller_id() != owner_thread || !impl->commands) { return 0; }
    return superpos_egp::signed_bits(impl->commands->oldest());
}

void SuperposLockstepServer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "input_bytes", "slots", "playout"), &SuperposLockstepServer::configure, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("accept_inputs", "slot", "payload"), &SuperposLockstepServer::accept_inputs);
    ClassDB::bind_method(D_METHOD("consume_inputs", "slot"), &SuperposLockstepServer::consume_inputs);
    ClassDB::bind_method(D_METHOD("get_playout_status", "slot"), &SuperposLockstepServer::get_playout_status);
    ClassDB::bind_method(D_METHOD("begin_tick", "tick"), &SuperposLockstepServer::begin_tick);
    ClassDB::bind_method(D_METHOD("set_command", "slot", "input", "processed_tick"), &SuperposLockstepServer::set_command, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("end_tick"), &SuperposLockstepServer::end_tick);
    ClassDB::bind_method(D_METHOD("pack_commands", "after_tick", "max_bytes", "recipient"), &SuperposLockstepServer::pack_commands, DEFVAL(880), DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("pack_command_window", "acknowledged_tick", "sent_tick", "max_bytes", "recipient", "redundant_ticks"), &SuperposLockstepServer::pack_command_window, DEFVAL(880), DEFVAL(-1), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("pack_keyframe"), &SuperposLockstepServer::pack_keyframe);
    ClassDB::bind_method(D_METHOD("reset_stream", "slot", "keyframe_tick"), &SuperposLockstepServer::reset_stream);
    ClassDB::bind_method(D_METHOD("pack_stream", "slot", "srtt_usec", "max_bytes"), &SuperposLockstepServer::pack_stream, DEFVAL(880));
    ClassDB::bind_method(D_METHOD("get_stream_status", "slot"), &SuperposLockstepServer::get_stream_status);
    ClassDB::bind_method(D_METHOD("get_streams_summary"), &SuperposLockstepServer::get_streams_summary);
    ClassDB::bind_method(D_METHOD("bind_session", "slot", "session"), &SuperposLockstepServer::bind_session);
    ClassDB::bind_method(D_METHOD("set_stream_enabled", "slot", "enabled"), &SuperposLockstepServer::set_stream_enabled);
    ClassDB::bind_method(D_METHOD("set_stream_interval", "slot", "ticks"), &SuperposLockstepServer::set_stream_interval);
    ClassDB::bind_method(D_METHOD("ingest_inputs", "channel", "max_per_session"), &SuperposLockstepServer::ingest_inputs, DEFVAL(8));
    ClassDB::bind_method(D_METHOD("step_commands", "tick", "hold_mask", "merge_mask"), &SuperposLockstepServer::step_commands);
    ClassDB::bind_method(D_METHOD("publish_commands", "channel", "server_tick", "max_bytes", "max_waiting"), &SuperposLockstepServer::publish_commands, DEFVAL(860), DEFVAL(2));
    ClassDB::bind_method(D_METHOD("get_command_tick"), &SuperposLockstepServer::get_command_tick);
    ClassDB::bind_method(D_METHOD("get_oldest_command_tick"), &SuperposLockstepServer::get_oldest_command_tick);
    ClassDB::bind_method(D_METHOD("get_command_totals"), &SuperposLockstepServer::get_command_totals);
}
