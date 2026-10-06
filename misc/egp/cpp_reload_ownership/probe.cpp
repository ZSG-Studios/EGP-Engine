// SPDX-License-Identifier: MIT
#include "egp_net.hpp"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
using namespace godot;
namespace net = egp::networking;

class EGPNetOwnershipProbe : public Node {
    GDCLASS(EGPNetOwnershipProbe, Node);
    std::unique_ptr<net::Net> server, client;
    std::unique_ptr<net::Box3D> box;
    Dictionary saved, capsules;
    bool require(bool value) {
        saved["checks"] = int64_t(saved.get("checks", 0)) + 1;
        return value;
    }
    void before(int64_t tick) { saved["before"] = int64_t(saved.get("before", 0)) + 1; }
    void after(int64_t tick) { saved["after"] = int64_t(saved.get("after", 0)) + 1; }
    void failed(int64_t error) { saved["failures"] = int64_t(saved.get("failures", 0)) + 1; }
    void on_message(int64_t peer, const Array &args) { saved["messages"] = int64_t(saved.get("messages", 0)) + 1; }
    Error subscribe() {
        auto callback = callable_mp(this, &EGPNetOwnershipProbe::before);
        if (box->connect("before_step", callback) != OK || server->connect("simulation_tick", callable_mp(this, &EGPNetOwnershipProbe::on_tick)) != OK) return FAILED;
        box->disconnect("before_step", callback); box->disconnect("before_step", callback);
        server->disconnect("simulation_tick", callable_mp(this, &EGPNetOwnershipProbe::on_tick));
        server->disconnect("simulation_tick", callable_mp(this, &EGPNetOwnershipProbe::on_tick));
        if (box->connect("before_step", callable_mp(this, &EGPNetOwnershipProbe::before)) != OK
            || box->connect("after_step", callable_mp(this, &EGPNetOwnershipProbe::after)) != OK
            || box->connect("failed", callable_mp(this, &EGPNetOwnershipProbe::failed)) != OK) return FAILED;
        return client->register_message("ownership", callable_mp(this, &EGPNetOwnershipProbe::on_message), net::Sender::Server);
    }
    void on_tick(int64_t tick, bool authority) { saved["unexpected_tick_callback"] = true; }
    template<class Owner> bool reject(Dictionary capsule) {
        Dictionary copy = capsule.duplicate();
        Error error = OK;
        auto result = Owner::resume_after_reload(capsule, &error);
        return require(!result && error == ERR_INVALID_PARAMETER && capsule == copy);
    }
    template<class Owner> bool malformed(const Dictionary &capsule, const char *object_key) {
        if (!reject<Owner>(Dictionary())) return false;
        for (const char *key : {"version", object_key, "token"}) {
            Dictionary missing = capsule.duplicate(); missing.erase(key);
            Dictionary wrong = capsule.duplicate(); wrong[key] = false;
            if (!reject<Owner>(missing) || !reject<Owner>(wrong)) return false;
        }
        Dictionary future = capsule.duplicate(); future["version"] = int64_t(4294967297LL);
        Dictionary forged = capsule.duplicate(); PackedByteArray token = forged["token"]; token = token.duplicate(); token[0] ^= 1; forged["token"] = token;
        Dictionary foreign = capsule.duplicate(); Ref<RefCounted> other; other.instantiate(); foreign[object_key] = other;
        return reject<Owner>(future) && reject<Owner>(forged) && reject<Owner>(foreign);
    }
protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("start"), &EGPNetOwnershipProbe::start);
        ClassDB::bind_method(D_METHOD("poll"), &EGPNetOwnershipProbe::poll);
        ClassDB::bind_method(D_METHOD("send"), &EGPNetOwnershipProbe::send);
        ClassDB::bind_method(D_METHOD("suspend"), &EGPNetOwnershipProbe::suspend);
        ClassDB::bind_method(D_METHOD("resume"), &EGPNetOwnershipProbe::resume);
        ClassDB::bind_method(D_METHOD("proof"), &EGPNetOwnershipProbe::proof);
        ClassDB::bind_method(D_METHOD("set_saved", "value"), &EGPNetOwnershipProbe::set_saved);
        ClassDB::bind_method(D_METHOD("get_saved"), &EGPNetOwnershipProbe::get_saved);
        ClassDB::bind_method(D_METHOD("set_capsules", "value"), &EGPNetOwnershipProbe::set_capsules);
        ClassDB::bind_method(D_METHOD("get_capsules"), &EGPNetOwnershipProbe::get_capsules);
        ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "saved"), "set_saved", "get_saved");
        ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "capsules"), "set_capsules", "get_capsules");
    }
public:
    ~EGPNetOwnershipProbe() { box.reset(); client.reset(); server.reset(); }
    void set_saved(const Dictionary &value) { saved = value; }
    Dictionary get_saved() const { return saved; }
    void set_capsules(const Dictionary &value) { capsules = value; }
    Dictionary get_capsules() const { return capsules; }
    int start() {
        Variant instance = ClassDBSingleton::get_singleton()->instantiate("EGPBox3DWorld");
        Object *object = instance; Ref<RefCounted> world(Object::cast_to<RefCounted>(object));
        if (!world.is_valid() || int64_t(world->call("configure", 60, 4, 1, Vector3(0, -9.8, 0))) != OK) return FAILED;
        saved["world"] = world;
        server = std::make_unique<net::Net>(*this); client = std::make_unique<net::Net>(*this);
        server->set_auto_poll(false); client->set_auto_poll(false);
        net::Options options; options.simulation_fingerprint = world->call("get_simulation_fingerprint");
        options.simulated_latency_ms = 10; options.simulated_jitter_ms = 2; options.simulated_loss = 3;
        options.private_key.resize(32); for (int i = 0; i < 32; ++i) options.private_key[i] = i + 1;
        if (server->configure(options) != OK || server->host(0, "127.0.0.1") != OK || client->configure(options) != OK) return FAILED;
        int64_t port = server->statistics()["local_port"];
        net::TokenResult token = server->issue_token(711, String("127.0.0.1:") + String::num_int64(port));
        if (token.error != OK || client->join_token(711, token.token, "127.0.0.1") != OK) return FAILED;
        Dictionary entity_state; entity_state["position"] = Vector3(0, 10000, 0);
        saved["entity"] = server->spawn(1, entity_state);
        if (!int64_t(saved["entity"]) || int64_t(world->call("queue_create_sphere", 10000, 1, Vector3(0, 10000, 0), 0.5)) != OK) return FAILED;
        box = std::make_unique<net::Box3D>();
        if (box->attach(*server, world) != OK || box->track(saved["entity"], 10000) != OK) return FAILED;
        saved["server_id"] = int64_t(server->bridge()->get_instance_id());
        saved["client_id"] = int64_t(client->bridge()->get_instance_id());
        saved["adapter_id"] = int64_t(box->native()->get_instance_id());
        saved["server_session_id"] = int64_t(server->native_session()->get_instance_id());
        saved["client_session_id"] = int64_t(client->native_session()->get_instance_id());
        return subscribe();
    }
    int poll() {
        if (!server || !client) return ERR_UNCONFIGURED;
        Error error = server->poll(); if (error != OK) return error;
        return client->poll();
    }
    int send() { return server ? server->broadcast_message("ownership") : ERR_UNCONFIGURED; }
    int suspend() {
        if (!server || !client || !box) return ERR_UNCONFIGURED;
        Dictionary a = server->detach_for_reload(), b = client->detach_for_reload(), c = box->detach_for_reload();
        if (!require(a.size() == 3 && b.size() == 3 && c.size() == 3)
            || !require(!server->available() && !client->available() && !box->available())
            || !require(server->detach_for_reload().is_empty() && box->detach_for_reload().is_empty())
            || !malformed<net::Net>(a, "bridge") || !malformed<net::Box3D>(c, "adapter")) return FAILED;
        Ref<RefCounted> adapter = c["adapter"];
        Object *client_object = b["bridge"];
        Node *client_bridge = Object::cast_to<Node>(client_object);
        if (!require(Array(adapter->get_signal_connection_list("before_step")).is_empty())
            || !require(Dictionary(client_bridge->get("_handlers")).is_empty())) return FAILED;
        capsules["server"] = a; capsules["client"] = b; capsules["box"] = c;
        capsules["copied_server"] = a.duplicate(); capsules["copied_box"] = c.duplicate();
        server.reset(); client.reset(); box.reset();
        saved["handoffs"] = int64_t(saved.get("handoffs", 0)) + 1;
        return OK;
    }
    int resume() {
        Dictionary a = capsules.get("server", Dictionary()), b = capsules.get("client", Dictionary()), c = capsules.get("box", Dictionary());
        server = net::Net::resume_after_reload(a); client = net::Net::resume_after_reload(b); box = net::Box3D::resume_after_reload(c);
        if (!require(server && client && box && a.is_empty() && b.is_empty() && c.is_empty())
            || !reject<net::Net>(capsules["copied_server"]) || !reject<net::Box3D>(capsules["copied_box"])
            || !reject<net::Net>(a) || !reject<net::Box3D>(c)) return FAILED;
        capsules.clear(); saved["restores"] = int64_t(saved.get("restores", 0)) + 1;
        return subscribe();
    }
    Dictionary proof() const {
        Dictionary out = saved.duplicate(); out["version"] = FIXTURE_VERSION;
        Ref<RefCounted> world = saved.get("world", Variant());
        out["world_id"] = int64_t(world->get_instance_id());
        out["world_tick"] = world->call("get_tick"); out["world_hash"] = world->call("get_state_hash");
        out["body"] = world->call("get_body_state", 10000); out["capsules"] = capsules.size();
        Dictionary body = out["body"]; Vector3 position = body.get("position", Vector3()); out["body_y"] = position.y;
        if (server && client && box) {
            out["server_live_id"] = int64_t(server->bridge()->get_instance_id()); out["client_live_id"] = int64_t(client->bridge()->get_instance_id());
            out["adapter_live_id"] = int64_t(box->native()->get_instance_id());
            out["server_session_live_id"] = int64_t(server->native_session()->get_instance_id());
            out["client_session_live_id"] = int64_t(client->native_session()->get_instance_id());
            out["server_state"] = server->state(); out["client_state"] = client->state();
            out["client_entity"] = client->entity(saved["entity"]);
            out["before_connections"] = Array(box->native()->get_signal_connection_list("before_step")).size();
            out["clock_connections"] = Array(server->bridge()->get_signal_connection_list("simulation_tick")).size();
            out["body_map"] = box->native()->get("_tracked");
        }
        return out;
    }
};
static void initialize(ModuleInitializationLevel level) { if (level == MODULE_INITIALIZATION_LEVEL_SCENE) GDREGISTER_CLASS(EGPNetOwnershipProbe); }
static void terminate(ModuleInitializationLevel level) {}
extern "C" GDExtensionBool GDE_EXPORT ownership_init(GDExtensionInterfaceGetProcAddress address, GDExtensionClassLibraryPtr library, GDExtensionInitialization *initialization) {
    GDExtensionBinding::InitObject init(address, library, initialization);
    init.register_initializer(initialize); init.register_terminator(terminate); init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE); return init.init();
}
