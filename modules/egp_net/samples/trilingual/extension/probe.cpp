// SPDX-License-Identifier: MIT
#include "egp_net.hpp"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <memory>
using namespace godot;
namespace net = egp::networking;
class EGPNetCppActor : public Node3D {
    GDCLASS(EGPNetCppActor, Node3D);
    net::EntityPresentation3D presentation;
protected:
    static void _bind_methods() { ClassDB::bind_method(D_METHOD("apply_network_state", "state"), &EGPNetCppActor::apply_network_state); }
public:
    EGPNetCppActor() : presentation(*this) { presentation.smoothing_speed = 0; }
    void apply_network_state(const Dictionary &state) { presentation.apply(state); }
    void _process(double delta) override { presentation.process(delta); }
};
class EGPNetCppProbe : public Node {
    GDCLASS(EGPNetCppProbe, Node);
    std::unique_ptr<net::Net> high;
    std::unique_ptr<net::Session> low;
    std::unique_ptr<net::Box3D> physics;
    Ref<RefCounted> world;
    int64_t body = 0;
    bool process_mode = false, process_sent = false, process_replied = false;
    int messages = 0, packets = 0, applications = 0, predicted = 0;
    Array clock_diagnostics;
    Array recovery_client_states;
    int64_t recovery_peer = 0, recovery_entity = 0;
    int recovery_cycle = 0, recovery_inputs = 0, recovery_invalid_inputs = 0;
    String clock_handoff, clock_message;
    int clock_epoch = 1, clock_drain = 0;
    int64_t clock_entity = 0, clock_retired = 0;
    bool clock_joined = false, clock_sent = false, clock_reply_seen = false, clock_done = false;
    Ref<RefCounted> clock_client_native;
    PackedByteArray clock_previous_token;
    Array clock_epochs, clock_disconnects, clock_states, clock_polls;
    void clock_state(const String &state) { clock_states.push_back(state); }
    void clock_reply(int64_t peer, const Array &args) {
        if (peer != 0 || args.size() != 1 || int64_t(args[0]) != clock_epoch) { clock_done = true; clock_message = "unexpected epoch reply"; }
        else clock_reply_seen = true;
    }
    void clock_diagnostic(const String &message) { clock_diagnostics.push_back(message); }
    void recovery_state(const String &state) { recovery_client_states.push_back(state); }
    void recovery_input(int64_t peer, int64_t entity, const Dictionary &input) {
        if (peer == recovery_peer && entity == recovery_entity && int64_t(input.get("cycle", 0)) == recovery_cycle) ++recovery_inputs;
        else ++recovery_invalid_inputs;
    }
    void message(int64_t peer, const Array &args) {
        if (args.size() == 2 && int64_t(args[0]) == 77 && Vector3(args[1]) == Vector3(1, 2, 3)) {
            ++messages; Array reply; reply.push_back("cpp"); reply.push_back(88);
            high->send_message(peer, "reply", reply);
        }
    }
    void packet(int64_t peer, const PackedByteArray &bytes, int64_t channel, int64_t delivery) {
        if (bytes.size() == 3 && bytes[0] == 4 && channel == 3 && delivery == 4) ++packets;
    }
    void application(int64_t peer, const PackedByteArray &bytes) {
        if (bytes.size() == 3 && bytes[0] == 9) { ++applications; low->send_application(peer, bytes); }
    }
    void process_reply(int64_t peer, const Array &args) { process_replied = peer == 0 && args.size() == 1 && int64_t(args[0]) == 88; }
    PackedByteArray capture() { PackedByteArray b; b.push_back(predicted); return b; }
    int restore(const PackedByteArray &b) { if (b.size() != 1) return ERR_INVALID_DATA; predicted = b[0]; return OK; }
    int simulate(int64_t tick, const PackedByteArray &b, bool replay) { predicted += b[0]; return OK; }
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("start", "token", "low_token"), &EGPNetCppProbe::start);
        ClassDB::bind_method(D_METHOD("poll"), &EGPNetCppProbe::poll);
        ClassDB::bind_method(D_METHOD("status"), &EGPNetCppProbe::status);
        ClassDB::bind_method(D_METHOD("prediction_check"), &EGPNetCppProbe::prediction_check);
        ClassDB::bind_method(D_METHOD("start_physics"), &EGPNetCppProbe::start_physics);
        ClassDB::bind_method(D_METHOD("start_process", "token"), &EGPNetCppProbe::start_process);
        ClassDB::bind_method(D_METHOD("stop"), &EGPNetCppProbe::stop);
        ClassDB::bind_method(D_METHOD("clock_recovery_check"), &EGPNetCppProbe::clock_recovery_check);
        ClassDB::bind_method(D_METHOD("start_clock_process", "handoff", "fingerprint"), &EGPNetCppProbe::start_clock_process);
        ClassDB::bind_method(D_METHOD("clock_process_tick"), &EGPNetCppProbe::clock_process_tick);
    }
public:
    int start(const PackedByteArray &token, const PackedByteArray &low_token) {
        high = std::make_unique<net::Net>(*this); high->set_auto_poll(false);
        if (high->configure() != OK || high->register_message("hello", callable_mp(this, &EGPNetCppProbe::message), net::Sender::Server) != OK) return FAILED;
        high->connect("packet_received", callable_mp(this, &EGPNetCppProbe::packet));
        if (high->join_token(333, token) != OK) return FAILED;
        low = std::make_unique<net::Session>();
        if (low->configure() != OK || low->connect("application_received", callable_mp(this, &EGPNetCppProbe::application)) != OK) return FAILED;
        return low->connect_token(444, low_token);
    }
    int poll() {
        if (high) { Error result = high->poll(); if (result != OK) return result; }
        if (low) { Error result = low->poll(); if (result != OK) return result; }
        if (process_mode && high && !process_sent && high->entities().size() == 1) {
            Dictionary record = high->entity(high->entities()[0]); Dictionary state = record["state"];
            if (int64_t(state.get("stamp", 0)) == 55 && Vector3(state.get("position", Vector3())) == Vector3(1, 2, 3)) {
                Array args; args.push_back(77); process_sent = high->send_message(0, "hello", args) == OK;
            }
        }
        return OK;
    }
    Dictionary status() {
        Dictionary d; d["messages"] = messages; d["packets"] = packets; d["applications"] = applications;
        d["entities"] = high ? high->entities() : Array();
        d["record"] = high && !high->entities().is_empty() ? high->entity(high->entities()[0]) : Dictionary();
        d["low_entities"] = low ? low->entities() : Array();
        d["physics_tick"] = world.is_valid() ? world->call("get_tick") : Variant(0);
        d["network_tick"] = high ? high->statistics().get("tick", 0) : Variant(0);
        d["body"] = world.is_valid() ? world->call("get_body_state", 20000) : Variant(Dictionary());
        d["process_passed"] = process_sent && process_replied;
        return d;
    }
    bool prediction_check() {
        net::Session unconfigured;
        if (unconfigured.spawn(1, PackedByteArray()).error != ERR_UNCONFIGURED || !unconfigured.peers().is_empty() || !unconfigured.entities().is_empty()) return false;
        Node2D *actor = memnew(Node2D); add_child(actor);
        net::EntityPresentation2D presentation(*actor); presentation.smoothing_speed = 0;
        Dictionary state; state["transform"] = Transform2D(0, Vector2(2, 3)); presentation.apply(state); presentation.process(0.1);
        bool presentation_passed = actor->get_global_position() == Vector2(2, 3); actor->queue_free();
        if (!presentation_passed) return false;
        net::Prediction p;
        if (p.configure(callable_mp(this, &EGPNetCppProbe::capture), callable_mp(this, &EGPNetCppProbe::restore), callable_mp(this, &EGPNetCppProbe::simulate), 0, 4, 1, 16) != OK) return false;
        PackedByteArray b; b.push_back(1);
        if (p.predict(1, b) != OK || p.predict(2, b) != OK || predicted != 2) return false;
        PackedByteArray correction; correction.push_back(5);
        if (p.reconcile(1, correction) != OK || predicted != 6 || p.pending_ticks() != 1 || p.history_bytes() != 2) return false;
        net::Box3D adapter; // Also exercise script construction/destruction.
        return p.reset(10, correction) == OK && p.pending_ticks() == 0;
    }
    int start_physics() {
        stop();
        Variant instance = ClassDBSingleton::get_singleton()->instantiate("EGPBox3DWorld");
        Object *object = instance; world = Ref<RefCounted>(Object::cast_to<RefCounted>(object));
        if (world.is_null() || int64_t(world->call("configure", 60, 4, 1, Vector3(0, -9.8, 0))) != OK) return FAILED;
        high = std::make_unique<net::Net>(*this); high->set_auto_poll(false);
        net::Options options; options.simulation_fingerprint = world->call("get_simulation_fingerprint");
        if (high->configure(options) != OK || high->host(0) != OK) return FAILED;
        Dictionary state; state["position"] = Vector3(0, 10, 0);
        body = high->spawn(1, state);
        if (!body || int64_t(world->call("queue_create_sphere", 20000, 1, Vector3(0, 10, 0), 0.5)) != OK) return FAILED;
        physics = std::make_unique<net::Box3D>();
        if (physics->attach(*high, world) != OK || physics->track(body) != OK || physics->track(body, 20000) != OK || physics->track(body, -1) != ERR_INVALID_PARAMETER) return FAILED;
        return OK;
    }
    int start_process(const PackedByteArray &token) {
        high = std::make_unique<net::Net>(*this); high->set_auto_poll(false);
        net::Options options; options.game_protocol = "egp-process-fixture-v1"; options.max_players = 2; options.max_entities = 2;
        if (high->configure(options) != OK || high->register_message("reply", callable_mp(this, &EGPNetCppProbe::process_reply), net::Sender::Server) != OK) return FAILED;
        process_mode = true; return high->join_token(9876, token);
    }
    int start_clock_process(const String &handoff, const String &fingerprint) {
        stop(); clock_handoff = handoff;
        high = std::make_unique<net::Net>(*this); high->set_auto_poll(false);
        net::Options options; options.simulation_fingerprint = fingerprint; options.max_players = 1; options.max_entities = 2;
        options.simulated_latency_ms = 20; options.simulated_jitter_ms = 5;
        if (high->connect("state_changed", callable_mp(this, &EGPNetCppProbe::clock_state)) != OK || high->configure(options) != OK
            || high->register_message("reply", callable_mp(this, &EGPNetCppProbe::clock_reply), net::Sender::Server) != OK) return FAILED;
        clock_client_native = high->native_session(); return OK;
    }
    Dictionary clock_process_tick() {
        auto finish = [&](bool passed, const String &message) {
            Dictionary r; r["done"] = true; r["passed"] = passed; r["role"] = "client"; r["language"] = "cpp"; r["pid"] = OS::get_singleton()->get_process_id();
            r["epochs"] = clock_epochs; r["disconnects"] = clock_disconnects; r["states"] = clock_states; r["poll_utc_ms"] = clock_polls; r["message"] = message; return r;
        };
        if (clock_done) return finish(false, clock_message);
        clock_polls.push_back(int64_t(Time::get_singleton()->get_unix_time_from_system() * 1000));
        if (poll() != OK) return finish(false, "independent client polling failed");
        if (high->state() == "Disconnected") {
            if (clock_epoch >= 4 || !clock_sent || !high->entities().is_empty() || high->send_input(clock_entity, Dictionary()) != ERR_UNCONFIGURED) return finish(false, "native disconnect baseline/input");
            Dictionary d; d["epoch"] = clock_epoch; d["entity"] = clock_entity; d["cleared"] = true; d["utc_ms"] = int64_t(Time::get_singleton()->get_unix_time_from_system() * 1000); clock_disconnects.push_back(d);
            high->stop(); ++clock_epoch; clock_retired = clock_entity; clock_joined = clock_sent = clock_reply_seen = false; clock_drain = 0;
        }
        if (!clock_joined) {
            const String path = clock_handoff.path_join("epoch-" + String::num_int64(clock_epoch) + ".bin");
            if (FileAccess::file_exists(path)) {
                PackedByteArray token = FileAccess::get_file_as_bytes(path);
                if (token.size() == 2048) {
                    if (token == clock_previous_token || high->join_token(9876, token) != OK || high->native_session() != clock_client_native) return finish(false, "fresh admission/session retention");
                    clock_previous_token = token; clock_joined = true;
                }
            }
        }
        if (high->state() == "Connected" && !clock_sent && high->entities().size() == 1) {
            clock_entity = high->entities()[0]; Dictionary record = high->entity(clock_entity); Dictionary state = record["state"];
            const int64_t tick = state.get("physics_tick", 0);
            if (tick > 0 && int64_t(state.get("epoch", 0)) == clock_epoch) {
                if (clock_entity == clock_retired || !high->entity(clock_retired).is_empty() || int64_t(record["authority_peer"]) <= 0) return finish(false, "fresh owned physics baseline");
                Dictionary input; input["epoch"] = clock_epoch; Array args; args.push_back(clock_epoch); args.push_back(clock_entity); args.push_back(tick);
                if (high->send_input(clock_entity, input) != OK || high->send_message(0, "ready", args) != OK) return finish(false, "owner input/ready");
                Dictionary d; d["epoch"] = clock_epoch; d["entity"] = clock_entity; d["physics_tick"] = tick; d["same_session"] = true; d["fresh_token"] = true; d["retired_absent"] = true; clock_epochs.push_back(d); clock_sent = true;
            }
        }
        if (clock_reply_seen && clock_drain == 0) {
            Array args; args.push_back(clock_epoch);
            if (high->send_message(0, "ack", args) != OK) return finish(false, "epoch reply acknowledgement"); clock_drain = 1;
        }
        if (clock_epoch == 4 && clock_drain > 0 && ++clock_drain >= 15) { high->stop(); return finish(true, "independent authority disconnect and fresh physics baseline"); }
        Dictionary pending; pending["done"] = false; return pending;
    }
    Dictionary clock_recovery_check() {
        Dictionary proof; Array cycles; proof["passed"] = false; proof["cycles"] = cycles;
        if (start_physics() != OK) return proof;
        clock_diagnostics.clear();
        if (high->connect("diagnostic", callable_mp(this, &EGPNetCppProbe::clock_diagnostic)) != OK) return proof;
        const auto session = high->native_session();
        const int port = int64_t(high->statistics()["local_port"]);
        net::Net client(*this); client.set_auto_poll(false);
        recovery_client_states.clear(); recovery_inputs = recovery_invalid_inputs = 0;
        net::Options client_options; client_options.simulation_fingerprint = high->simulation_fingerprint();
        client_options.simulated_latency_ms = 20; client_options.simulated_jitter_ms = 5;
        if (client.connect("state_changed", callable_mp(this, &EGPNetCppProbe::recovery_state)) != OK || client.configure(client_options) != OK
            || high->connect("input_received", callable_mp(this, &EGPNetCppProbe::recovery_input)) != OK) return proof;
        const auto client_session = client.native_session();
        PackedByteArray previous_token;
        auto pump_until = [&](auto ready) {
            const uint64_t deadline = Time::get_singleton()->get_ticks_msec() + 2000;
            while (!ready() && Time::get_singleton()->get_ticks_msec() < deadline) {
                if (poll() != OK || client.poll() != OK) return false;
                OS::get_singleton()->delay_usec(2000);
            }
            return ready();
        };
        auto join_client = [&]() {
            auto token = high->issue_token(888, String("127.0.0.1:") + String::num_int64(port));
            if (token.error != OK || token.token.size() != 2048 || token.token == previous_token || client.join_token(888, token.token) != OK) return false;
            previous_token = token.token;
            if (!pump_until([&]() { return client.state() == "Connected" && high->peers().size() == 1; })) return false;
            Dictionary peer = high->peers()[0];
            return int64_t(peer["client_id"]) == 888 && client.native_session() == client_session;
        };
        auto pump_ticks = [&](int minimum) {
            return pump_until([&]() { return int64_t(high->statistics()["tick"]) >= minimum; });
        };
        if (!join_client() || !pump_ticks(8) || !pump_until([&]() { return !client.entity(body).is_empty(); })) return proof;
        for (int cycle = 1; cycle <= 3; ++cycle) {
            const int64_t retired = body;
            PackedByteArray snapshot = world->call("capture_snapshot");
            const int64_t checkpoint_tick = world->call("get_tick");
            const String checkpoint_hash = world->call("get_state_hash");
            Dictionary checkpoint_body = world->call("get_body_state", 20000);
            const double checkpoint_y = Vector3(checkpoint_body["position"]).y;
            if (snapshot.is_empty()) return proof;
            const uint64_t before = Time::get_singleton()->get_ticks_msec();
            int live_polls = 0;
            while (Time::get_singleton()->get_ticks_msec() - before < 550) {
                if (client.poll() != OK) return proof;
                ++live_polls; OS::get_singleton()->delay_usec(2000);
            }
            if (client.state() != "Connected" || live_polls == 0) return proof;
            const int error = poll();
            const uint64_t gap_ms = Time::get_singleton()->get_ticks_msec() - before;
            if (error != FAILED || high->state() != "Stopped" || !high->entities().is_empty() || !high->peers().is_empty() || int64_t(high->statistics()["tick"]) != 0 || high->spawn(1) != 0 || high->update_entity(retired, Dictionary()) != ERR_UNAUTHORIZED) return proof;
            client.stop();
            if (client.state() != "Stopped" || !client.entities().is_empty() || client.send_input(retired, Dictionary()) != ERR_UNCONFIGURED) return proof;
            physics->detach();
            if (int64_t(world->call("step_tick", checkpoint_tick + 1)) != OK || int64_t(world->call("restore_snapshot", snapshot)) != OK || String(world->call("get_state_hash")) != checkpoint_hash || int64_t(world->call("get_tick")) != checkpoint_tick) return proof;
            if (high->host(port) != OK || high->native_session() != session || !high->entity(retired).is_empty() || high->update_entity(retired, Dictionary()) != ERR_DOES_NOT_EXIST) return proof;
            if (physics->attach(*high, world) != OK || !join_client()) return proof;
            Dictionary peer = high->peers()[0]; recovery_peer = peer["peer_id"]; recovery_cycle = cycle;
            body = high->spawn(1, Dictionary(), recovery_peer); recovery_entity = body;
            if (!body || body == retired || physics->track(body, 20000) != OK || !pump_ticks(8)
                || !pump_until([&]() { Dictionary r = client.entity(body); Dictionary s = r.get("state", Dictionary()); return int64_t(s.get("physics_tick", 0)) > checkpoint_tick; })) return proof;
            Dictionary state = high->entity(body)["state"];
            if (int64_t(state.get("physics_tick", 0)) <= checkpoint_tick || int64_t(world->call("get_tick")) != checkpoint_tick + int64_t(high->statistics()["tick"])) return proof;
            Dictionary client_record = client.entity(body); Dictionary client_state = client_record["state"];
            const int64_t client_tick = client_state["physics_tick"];
            if (client.entities().size() != 1 || !client.entity(retired).is_empty() || int64_t(client_record["authority_peer"]) != recovery_peer
                || client_tick <= checkpoint_tick || Vector3(client_state["position"]).y >= checkpoint_y) return proof;
            Dictionary input; input["cycle"] = cycle;
            if (client.send_input(retired, input) != OK || client.send_input(body, input) != OK
                || !pump_until([&]() { return recovery_inputs >= cycle; }) || recovery_inputs != cycle || recovery_invalid_inputs != 0) return proof;
            if (high->set_entity_visible(body, recovery_peer, false) != OK || !pump_until([&]() { return client.entity(body).is_empty(); })
                || high->set_entity_visible(body, recovery_peer, true) != OK || !pump_until([&]() { return !client.entity(body).is_empty(); })) return proof;
            Dictionary record; record["cycle"] = cycle; record["old_entity"] = retired; record["new_entity"] = body; record["checkpoint_tick"] = checkpoint_tick; record["checkpoint_hash"] = checkpoint_hash; record["final_physics_tick"] = world->call("get_tick"); record["final_network_tick"] = high->statistics()["tick"]; record["gap_ms"] = int64_t(gap_ms); record["poll_error"] = error;
            record["client_live_polls"] = live_polls; record["client_id"] = 888; record["client_same_session"] = true; record["fresh_token"] = true;
            record["client_reset_cleared"] = true; record["client_retired_absent"] = true; record["client_physics_tick"] = client_tick;
            record["owner_input_count"] = recovery_inputs; record["invalid_input_count"] = recovery_invalid_inputs; record["interest_roundtrip"] = true;
            cycles.push_back(record);
        }
        if (clock_diagnostics.size() != 3) return proof;
        for (int i = 0; i < 3; ++i) if (String(clock_diagnostics[i]) != "Fixed simulation exceeded its catch-up budget; resynchronization required.") return proof;
        physics->detach(); high->stop(); client.stop();
        Array high_diagnostics = clock_diagnostics.duplicate(); clock_diagnostics.clear();
        low = std::make_unique<net::Session>();
        if (low->configure() != OK || low->connect("diagnostic", callable_mp(this, &EGPNetCppProbe::clock_diagnostic)) != OK || low->listen(0) != OK) return proof;
        const auto native = low->native();
        const int low_port = int64_t(low->statistics()["local_port"]);
        int64_t previous = low->spawn(1, PackedByteArray()).entity;
        Array low_cycles;
        for (int cycle = 1; cycle <= 3; ++cycle) {
            const uint64_t before = Time::get_singleton()->get_ticks_msec();
            OS::get_singleton()->delay_usec(550000);
            if (poll() != FAILED || low->state() != "Stopped" || !low->entities().is_empty() || low->spawn(1, PackedByteArray()).error != ERR_UNAUTHORIZED || low->listen(low_port) != OK || low->native() != native) return proof;
            auto next = low->spawn(1, PackedByteArray());
            if (next.error != OK || !next.entity || next.entity == previous || low->update_entity(previous, PackedByteArray()) != ERR_DOES_NOT_EXIST) return proof;
            Dictionary record; record["cycle"] = cycle; record["old_entity"] = previous; record["new_entity"] = next.entity; record["poll_error"] = FAILED; record["gap_ms"] = int64_t(Time::get_singleton()->get_ticks_msec() - before); low_cycles.push_back(record); previous = next.entity;
        }
        if (clock_diagnostics.size() != 3) return proof;
        for (int i = 0; i < 3; ++i) if (String(clock_diagnostics[i]) != "Fixed simulation exceeded its catch-up budget; resynchronization required.") return proof;
        proof["passed"] = true; proof["cycles"] = cycles; proof["low_cycles"] = low_cycles; proof["diagnostics"] = high_diagnostics; proof["low_diagnostics"] = clock_diagnostics; proof["same_session"] = true; proof["body_id"] = 20000;
        proof["client_states"] = recovery_client_states; proof["client_latency_ms"] = 20; proof["client_jitter_ms"] = 5;
        stop(); return proof;
    }
    void stop() { physics.reset(); world.unref(); high.reset(); low.reset(); }
    void _exit_tree() override { stop(); }
};
static void initialize(ModuleInitializationLevel level) { if (level == MODULE_INITIALIZATION_LEVEL_SCENE) { GDREGISTER_CLASS(EGPNetCppProbe); GDREGISTER_CLASS(EGPNetCppActor); } }
static void terminate(ModuleInitializationLevel level) {}
extern "C" GDExtensionBool GDE_EXPORT netinterop_init(GDExtensionInterfaceGetProcAddress address, GDExtensionClassLibraryPtr library, GDExtensionInitialization *initialization) {
    GDExtensionBinding::InitObject init(address, library, initialization);
    init.register_initializer(initialize); init.register_terminator(terminate); init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE); return init.init();
}
