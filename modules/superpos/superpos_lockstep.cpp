// SPDX-License-Identifier: MIT
#include "superpos_lockstep.h"
#include "u64_bits.h"
#include "core/object/class_db.h"
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
    std::optional<superpos::InputHistory> history;
    std::optional<superpos::CommandDecoder> commands;
    int64_t slots = 0;
};

SuperposLockstepClient::SuperposLockstepClient() { impl = memnew(Impl); }
SuperposLockstepClient::~SuperposLockstepClient() { memdelete(impl); }

Error SuperposLockstepClient::configure(int64_t p_input_bytes, int64_t p_slots) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (p_input_bytes < 1 || p_input_bytes > int64_t(superpos::lockstep_input_bytes) || p_slots < 1 || p_slots > int64_t(superpos::lockstep_slots)) { return ERR_INVALID_PARAMETER; }
    auto history = superpos::InputHistory::create(size_t(p_input_bytes));
    auto commands = superpos::CommandDecoder::create(size_t(p_input_bytes), size_t(p_slots));
    if (!history || !commands) { return ERR_INVALID_PARAMETER; }
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
PackedByteArray SuperposLockstepClient::pack_inputs(int64_t p_max_ticks, int64_t p_max_bytes) const {
    if (Thread::get_caller_id() != owner_thread || !impl->history || p_max_ticks < 0 || p_max_bytes < 8 || p_max_bytes > wire_ceiling) { return PackedByteArray(); }
    std::array<std::byte, wire_ceiling> buffer{};
    auto encoded = impl->history->encode(impl->commands->applied(), size_t(p_max_ticks), std::span(buffer).first(size_t(p_max_bytes)));
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
    return result;
}

void SuperposLockstepClient::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "input_bytes", "slots"), &SuperposLockstepClient::configure);
    ClassDB::bind_method(D_METHOD("record_input", "tick", "input"), &SuperposLockstepClient::record_input);
    ClassDB::bind_method(D_METHOD("acknowledge_inputs", "tick"), &SuperposLockstepClient::acknowledge_inputs);
    ClassDB::bind_method(D_METHOD("pack_inputs", "max_ticks", "max_bytes"), &SuperposLockstepClient::pack_inputs, DEFVAL(60), DEFVAL(880));
    ClassDB::bind_method(D_METHOD("accept_commands", "payload"), &SuperposLockstepClient::accept_commands);
    ClassDB::bind_method(D_METHOD("load_keyframe", "table"), &SuperposLockstepClient::load_keyframe);
    ClassDB::bind_method(D_METHOD("advance_command"), &SuperposLockstepClient::advance_command);
    ClassDB::bind_method(D_METHOD("get_input", "slot"), &SuperposLockstepClient::get_input);
    ClassDB::bind_method(D_METHOD("get_status"), &SuperposLockstepClient::get_status);
}

struct SuperposLockstepServer::Impl {
    superpos::PlayoutConfig playout;
    std::array<std::unique_ptr<superpos::InputPlayout>, superpos::lockstep_slots> peers;
    std::optional<superpos::CommandEncoder> commands;
    int64_t slots = 0;
};

SuperposLockstepServer::SuperposLockstepServer() { impl = memnew(Impl); }
SuperposLockstepServer::~SuperposLockstepServer() { memdelete(impl); }

Error SuperposLockstepServer::configure(int64_t p_input_bytes, int64_t p_slots, const Dictionary &p_playout) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (p_input_bytes < 1 || p_input_bytes > int64_t(superpos::lockstep_input_bytes) || p_slots < 1 || p_slots > int64_t(superpos::lockstep_slots)) { return ERR_INVALID_PARAMETER; }
    for (const Variant &key : p_playout.keys()) {
        const String name = key;
        if (name != "initial_target" && name != "minimum_target" && name != "maximum_target" && name != "relax_ticks" && name != "catch_up_margin") { return ERR_INVALID_PARAMETER; }
    }
    superpos::PlayoutConfig config;
    config.input_bytes = size_t(p_input_bytes);
    config.initial_target = size_t(int64_t(p_playout.get("initial_target", int64_t(config.initial_target))));
    config.minimum_target = size_t(int64_t(p_playout.get("minimum_target", int64_t(config.minimum_target))));
    config.maximum_target = size_t(int64_t(p_playout.get("maximum_target", int64_t(config.maximum_target))));
    config.relax_ticks = superpos::Tick(int64_t(p_playout.get("relax_ticks", int64_t(config.relax_ticks))));
    config.catch_up_margin = size_t(int64_t(p_playout.get("catch_up_margin", int64_t(config.catch_up_margin))));
    if (!superpos::InputPlayout::create(config)) { return ERR_INVALID_PARAMETER; }
    auto commands = superpos::CommandEncoder::create(size_t(p_input_bytes), size_t(p_slots));
    if (!commands) { return ERR_INVALID_PARAMETER; }
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
Error SuperposLockstepServer::set_command(int64_t p_slot, const PackedByteArray &p_input) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (!impl->commands) { return ERR_UNCONFIGURED; }
    if (p_slot < 0 || p_slot >= impl->slots) { return ERR_INVALID_PARAMETER; }
    auto set = impl->commands->set(uint16_t(p_slot), bytes_of(p_input));
    return set ? OK : lockstep_error(set.error());
}
Error SuperposLockstepServer::end_tick() {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (!impl->commands) { return ERR_UNCONFIGURED; }
    auto ended = impl->commands->end();
    return ended ? OK : lockstep_error(ended.error());
}
Dictionary SuperposLockstepServer::pack_commands(int64_t p_after_tick, int64_t p_max_bytes) const {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    result["payload"] = PackedByteArray();
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (!impl->commands) { return result; }
    if (p_after_tick < 0 || p_max_bytes < 8 || p_max_bytes > wire_ceiling) { result["error"] = ERR_INVALID_PARAMETER; return result; }
    std::array<std::byte, wire_ceiling> buffer{};
    auto encoded = impl->commands->encode(superpos::Tick(p_after_tick), std::span(buffer).first(size_t(p_max_bytes)));
    if (!encoded) { result["error"] = lockstep_error(encoded.error()); return result; }
    result["error"] = OK;
    result["payload"] = packed(std::span<const std::byte>(buffer).first(*encoded));
    return result;
}
PackedByteArray SuperposLockstepServer::pack_keyframe() const {
    if (Thread::get_caller_id() != owner_thread || !impl->commands) { return PackedByteArray(); }
    std::array<std::byte, wire_ceiling> buffer{};
    auto encoded = impl->commands->encode_table(buffer);
    return encoded ? packed(std::span<const std::byte>(buffer).first(*encoded)) : PackedByteArray();
}
int64_t SuperposLockstepServer::get_command_tick() const {
    if (Thread::get_caller_id() != owner_thread || !impl->commands) { return 0; }
    return superpos_egp::signed_bits(impl->commands->newest());
}

void SuperposLockstepServer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "input_bytes", "slots", "playout"), &SuperposLockstepServer::configure, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("accept_inputs", "slot", "payload"), &SuperposLockstepServer::accept_inputs);
    ClassDB::bind_method(D_METHOD("consume_inputs", "slot"), &SuperposLockstepServer::consume_inputs);
    ClassDB::bind_method(D_METHOD("get_playout_status", "slot"), &SuperposLockstepServer::get_playout_status);
    ClassDB::bind_method(D_METHOD("begin_tick", "tick"), &SuperposLockstepServer::begin_tick);
    ClassDB::bind_method(D_METHOD("set_command", "slot", "input"), &SuperposLockstepServer::set_command);
    ClassDB::bind_method(D_METHOD("end_tick"), &SuperposLockstepServer::end_tick);
    ClassDB::bind_method(D_METHOD("pack_commands", "after_tick", "max_bytes"), &SuperposLockstepServer::pack_commands, DEFVAL(880));
    ClassDB::bind_method(D_METHOD("pack_keyframe"), &SuperposLockstepServer::pack_keyframe);
    ClassDB::bind_method(D_METHOD("get_command_tick"), &SuperposLockstepServer::get_command_tick);
}
