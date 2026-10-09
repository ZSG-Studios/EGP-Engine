// SPDX-License-Identifier: MIT
#include "egp_net.hpp"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
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
    void poll() {
        if (high) high->poll(); if (low) low->poll();
        if (process_mode && high && !process_sent && high->entities().size() == 1) {
            Dictionary record = high->entity(high->entities()[0]); Dictionary state = record["state"];
            if (int64_t(state.get("stamp", 0)) == 55 && Vector3(state.get("position", Vector3())) == Vector3(1, 2, 3)) {
                Array args; args.push_back(77); process_sent = high->send_message(0, "hello", args) == OK;
            }
        }
    }
    Dictionary status() {
        Dictionary d; d["messages"] = messages; d["packets"] = packets; d["applications"] = applications;
        d["entities"] = high ? high->entities() : Array();
        d["record"] = high && !high->entities().is_empty() ? high->entity(high->entities()[0]) : Dictionary();
        d["low_entities"] = low ? low->entities() : Array();
        d["physics_tick"] = world.is_valid() ? world->call("get_tick") : Variant(0);
        d["network_tick"] = high ? high->statistics().get("tick", 0) : Variant(0);
        d["body"] = world.is_valid() ? world->call("get_body_state", body) : Variant(Dictionary());
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
        if (!body || int64_t(world->call("queue_create_sphere", body, 1, Vector3(0, 10, 0), 0.5)) != OK) return FAILED;
        physics = std::make_unique<net::Box3D>();
        return physics->attach(*high, world) == OK ? physics->track(body) : FAILED;
    }
    int start_process(const PackedByteArray &token) {
        high = std::make_unique<net::Net>(*this); high->set_auto_poll(false);
        net::Options options; options.game_protocol = "egp-process-fixture-v1"; options.max_players = 2; options.max_entities = 2;
        if (high->configure(options) != OK || high->register_message("reply", callable_mp(this, &EGPNetCppProbe::process_reply), net::Sender::Server) != OK) return FAILED;
        process_mode = true; return high->join_token(9876, token);
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
