// SPDX-License-Identifier: MIT
#include "egp_net.hpp"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
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
    void clock_diagnostic(const String &message) { clock_diagnostics.push_back(message); }
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
    Dictionary clock_recovery_check() {
        Dictionary proof; Array cycles; proof["passed"] = false; proof["cycles"] = cycles;
        if (start_physics() != OK) return proof;
        clock_diagnostics.clear();
        if (high->connect("diagnostic", callable_mp(this, &EGPNetCppProbe::clock_diagnostic)) != OK) return proof;
        const auto session = high->native_session();
        const int port = int64_t(high->statistics()["local_port"]);
        auto pump_ticks = [&](int minimum) {
            const uint64_t deadline = Time::get_singleton()->get_ticks_msec() + 2000;
            while (int64_t(high->statistics()["tick"]) < minimum && Time::get_singleton()->get_ticks_msec() < deadline) {
                if (poll() != OK) return false;
                OS::get_singleton()->delay_usec(2000);
            }
            return int64_t(high->statistics()["tick"]) >= minimum;
        };
        if (!pump_ticks(8)) return proof;
        for (int cycle = 1; cycle <= 3; ++cycle) {
            const int64_t retired = body;
            PackedByteArray snapshot = world->call("capture_snapshot");
            const int64_t checkpoint_tick = world->call("get_tick");
            const String checkpoint_hash = world->call("get_state_hash");
            if (snapshot.is_empty()) return proof;
            const uint64_t before = Time::get_singleton()->get_ticks_msec();
            OS::get_singleton()->delay_usec(550000);
            const int error = poll();
            const uint64_t gap_ms = Time::get_singleton()->get_ticks_msec() - before;
            if (error != FAILED || high->state() != "Stopped" || !high->entities().is_empty() || !high->peers().is_empty() || int64_t(high->statistics()["tick"]) != 0 || high->spawn(1) != 0 || high->update_entity(retired, Dictionary()) != ERR_UNAUTHORIZED) return proof;
            physics->detach();
            if (int64_t(world->call("step_tick", checkpoint_tick + 1)) != OK || int64_t(world->call("restore_snapshot", snapshot)) != OK || String(world->call("get_state_hash")) != checkpoint_hash || int64_t(world->call("get_tick")) != checkpoint_tick) return proof;
            if (high->host(port) != OK || high->native_session() != session || !high->entity(retired).is_empty() || high->update_entity(retired, Dictionary()) != ERR_DOES_NOT_EXIST) return proof;
            body = high->spawn(1, Dictionary());
            if (!body || body == retired || physics->attach(*high, world) != OK || physics->track(body, 20000) != OK || !pump_ticks(8)) return proof;
            Dictionary state = high->entity(body)["state"];
            if (int64_t(state.get("physics_tick", 0)) <= checkpoint_tick || int64_t(world->call("get_tick")) != checkpoint_tick + int64_t(high->statistics()["tick"])) return proof;
            Dictionary record; record["cycle"] = cycle; record["old_entity"] = retired; record["new_entity"] = body; record["checkpoint_tick"] = checkpoint_tick; record["checkpoint_hash"] = checkpoint_hash; record["final_physics_tick"] = world->call("get_tick"); record["final_network_tick"] = high->statistics()["tick"]; record["gap_ms"] = int64_t(gap_ms); record["poll_error"] = error;
            cycles.push_back(record);
        }
        if (clock_diagnostics.size() != 3) return proof;
        for (int i = 0; i < 3; ++i) if (String(clock_diagnostics[i]) != "Fixed simulation exceeded its catch-up budget; resynchronization required.") return proof;
        physics->detach(); high->stop();
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
