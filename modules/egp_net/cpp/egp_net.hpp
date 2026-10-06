// SPDX-License-Identifier: MIT
#pragma once
#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/crypto.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <vector>
#include <utility>
#include <cmath>
#include <algorithm>
#include <memory>

/// Godot C++ extension API. Engine-independent users can instead include net_core.h.
namespace egp::networking {
using namespace godot;
enum class Delivery : int { ReliableOrdered = 2, Unreliable = 4 };
enum class Sender : int { Server = 1, Client = 2, Both = 3 };
struct Options {
    int tick_rate = 60, max_players = 32, max_entities = 1024;
    int messages_per_second = 1000, bytes_per_second = 4 * 1024 * 1024;
    int timeout_seconds = 5, token_lifetime_seconds = 30;
    String game_protocol = "egp-game-v1", simulation_fingerprint = "script-state-v1";
    bool allow_insecure_loopback = false;
    PackedByteArray private_key;
    float simulated_loss = 0, simulated_latency_ms = 0, simulated_jitter_ms = 0;
    Dictionary dictionary() const {
        Dictionary d;
        d["tick_rate"] = tick_rate; d["max_players"] = max_players; d["max_entities"] = max_entities;
        d["messages_per_second"] = messages_per_second; d["bytes_per_second"] = bytes_per_second;
        d["timeout_seconds"] = timeout_seconds; d["token_lifetime_seconds"] = token_lifetime_seconds;
        d["game_protocol"] = game_protocol; d["simulation_fingerprint"] = simulation_fingerprint;
        d["allow_insecure_loopback"] = allow_insecure_loopback; d["simulated_loss"] = simulated_loss;
        d["simulated_latency_ms"] = simulated_latency_ms; d["simulated_jitter_ms"] = simulated_jitter_ms;
        if (!private_key.is_empty()) d["private_key"] = private_key;
        return d;
    }
};
struct TokenResult {
    Error error = ERR_UNCONFIGURED;
    PackedByteArray token;
    static TokenResult read(const Dictionary &d) {
        return {Error(int64_t(d.get("error", ERR_UNCONFIGURED))), d.get("token", PackedByteArray())};
    }
};
struct SpawnResult { Error error; int64_t entity; };
namespace detail {
inline Variant new_script(const char *path) {
    Ref<Script> script = ResourceLoader::get_singleton()->load(path);
    if (script.is_null()) return Variant();
    return script->call("new");
}
inline Ref<RefCounted> new_ref_script(const char *path) {
    // Retain the returned Variant until the Ref owns its own native reference.
    Variant instance = new_script(path);
    Object *object = instance;
    return Ref<RefCounted>(Object::cast_to<RefCounted>(object));
}
template<class... Args> Error error(Object *object, const char *method, const Args &...args) {
    if (!object) return ERR_UNCONFIGURED;
    Variant keep_alive(object);
    return Error(int64_t(object->call(method, args...)));
}
inline PackedByteArray reload_token() {
    Ref<Crypto> crypto;
    crypto.instantiate();
    return crypto.is_valid() ? crypto->generate_random_bytes(32) : PackedByteArray();
}
inline Object *reload_object(const Dictionary &state, const char *key, const char *path, const char *metadata) {
    Variant version = state.get("version", Variant());
    Variant object = state.get(key, Variant());
    Variant token = state.get("token", Variant());
    if (version.get_type() != Variant::INT || int64_t(version) != 1 || object.get_type() != Variant::OBJECT || token.get_type() != Variant::PACKED_BYTE_ARRAY) return nullptr;
    PackedByteArray bytes = token;
    Object *instance = object;
    Ref<Script> script = ResourceLoader::get_singleton()->load(path);
    if (!instance || script.is_null() || instance->get_script() != Variant(script) || bytes.size() != 32 || !instance->has_meta(metadata) || instance->get_meta(metadata) != token) return nullptr;
    return instance;
}
}

/// Native low-level session. Thread-affine; opaque state bytes and four raw channels.
class Session {
    Ref<RefCounted> value;
public:
    Session() {
        Variant instance = ClassDBSingleton::get_singleton()->instantiate("EGPNetSession");
        Object *object = instance;
        value = Ref<RefCounted>(Object::cast_to<RefCounted>(object));
    }
    ~Session() { close(); }
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    Ref<RefCounted> native() const { return value; }
    bool available() const { return value.is_valid(); }
    Error configure(const Options &options = {}) { return configure(options.dictionary()); }
    Error configure(const Dictionary &options) { return detail::error(value.ptr(), "configure", options); }
    Error listen(int port = 10515, const String &bind = "0.0.0.0") { return detail::error(value.ptr(), "listen", port, bind); }
    Error connect_loopback(const String &address, int port = 10515) { return detail::error(value.ptr(), "connect_to_server", address, port); }
    Error connect_token(int64_t client, const PackedByteArray &token, const String &bind = "0.0.0.0") { return detail::error(value.ptr(), "connect_token", client, token, bind); }
    TokenResult issue_token(int64_t client, const String &address) { return available() ? TokenResult::read(value->call("issue_token", client, address)) : TokenResult{}; }
    Error poll() { return detail::error(value.ptr(), "poll"); }
    void stop() { if (available()) value->call("stop"); }
    void close() { if (available()) value->call("close"); }
    String state() const { return available() ? String(value->call("get_state")) : String("Unconfigured"); }
    String fingerprint() const { return available() ? String(value->call("get_fingerprint")) : String(); }
    Dictionary statistics() const { return available() ? Dictionary(value->call("get_statistics")) : Dictionary(); }
    Variant command(const StringName &operation, const Dictionary &args = {}) { return available() ? value->call("command", operation, args) : Variant(ERR_UNCONFIGURED); }
    Error send_application(int64_t peer, const PackedByteArray &data) { return detail::error(value.ptr(), "send_application", peer, data); }
    Error send_packet(int64_t peer, const PackedByteArray &data, int channel = 0, Delivery delivery = Delivery::ReliableOrdered) {
        Dictionary d; d["peer"] = peer; d["payload"] = data; d["channel"] = channel; d["delivery"] = int(delivery);
        return Error(int64_t(command("send_packet", d)));
    }
    SpawnResult spawn(int kind, const PackedByteArray &state, int64_t authority = -1) {
        if (!available()) return {ERR_UNCONFIGURED, 0};
        Dictionary d; d["kind"] = kind; d["state"] = state; d["authority_peer"] = authority;
        Variant result = command("spawn", d);
        if (result.get_type() != Variant::DICTIONARY) return {Error(int64_t(result)), 0};
        Dictionary row = result; return {Error(int64_t(row["error"])), int64_t(row["entity"])};
    }
    Error update_entity(int64_t entity, const PackedByteArray &state) { Dictionary d; d["entity"] = entity; d["state"] = state; return Error(int64_t(command("update_entity", d))); }
    Error despawn(int64_t entity) { Dictionary d; d["entity"] = entity; return Error(int64_t(command("despawn", d))); }
    Error set_entity_visible(int64_t entity, int64_t peer, bool visible) { Dictionary d; d["entity"] = entity; d["peer"] = peer; d["visible"] = visible; return Error(int64_t(command("set_visible", d))); }
    Error disconnect_peer(int64_t peer) { Dictionary d; d["peer"] = peer; return Error(int64_t(command("disconnect", d))); }
    Array peers() { Variant rows = command("peers"); return rows.get_type() == Variant::ARRAY ? Array(rows) : Array(); }
    Array entities() { Variant rows = command("entities"); return rows.get_type() == Variant::ARRAY ? Array(rows) : Array(); }
    Error connect(const StringName &signal, const Callable &callback) { return available() ? value->connect(signal, callback) : ERR_UNCONFIGURED; }
    void disconnect(const StringName &signal, const Callable &callback) { if (available() && value->is_connected(signal, callback)) value->disconnect(signal, callback); }
};

/// AIO bridge owned as a child of the supplied game node. Its destruction is deferred.
/// Bind actor apply_network_state(Dictionary) to use register_scene with native actors.
class Net {
    uint64_t id = 0;
    std::vector<std::pair<StringName, Callable>> links;
    std::vector<StringName> messages;
    static constexpr const char *reload_metadata = "_egp_cpp_net_reload_token";
    explicit Net(Node *node) : id(node->get_instance_id()) {}
    void disconnect_callbacks() {
        if (auto *node = bridge()) {
            for (const auto &link : links) if (node->is_connected(link.first, link.second)) node->disconnect(link.first, link.second);
            for (const auto &message : messages) node->call("unregister_message", message);
        }
        links.clear(); messages.clear();
    }
public:
    explicit Net(Node &parent) {
        Variant instance = detail::new_script("res://addons/egp_net/egp_net.gd");
        Object *object = instance;
        Node *node = Object::cast_to<Node>(object);
        if (node) { id = node->get_instance_id(); node->set_name("EGPNetBridge"); parent.add_child(node); }
    }
    Net(const Net &) = delete;
    Net &operator=(const Net &) = delete;
    ~Net() {
        disconnect_callbacks();
        if (auto *node = bridge()) {
            node->call("close"); node->queue_free();
        }
    }
    /// Transfer the live bridge/session without closing it. Call at a Godot-thread safe boundary before unloading this extension.
    /// The original parent must survive. Old wrapper callbacks are removed; register them again after resume.
    /// Empty means unavailable/token generation failed, with ownership unchanged. Capsules are local references, never a wire/disk format.
    Dictionary detach_for_reload() {
        Dictionary state;
        auto *node = bridge();
        if (!node || node->is_queued_for_deletion()) return state;
        PackedByteArray token = detail::reload_token();
        if (token.size() != 32) return state;
        state["version"] = int64_t(1); state["bridge"] = node; state["token"] = token;
        node->set_meta(reload_metadata, token);
        disconnect_callbacks(); id = 0;
        return state;
    }
    /// Consume one authentic capsule. Invalid, foreign and copied/consumed capsules return null and remain unchanged.
    static std::unique_ptr<Net> resume_after_reload(Dictionary &state, Error *error = nullptr) {
        auto *node = Object::cast_to<Node>(detail::reload_object(state, "bridge", "res://addons/egp_net/egp_net.gd", reload_metadata));
        if (!node || node->is_queued_for_deletion() || !node->get_parent()) {
            if (error) *error = ERR_INVALID_PARAMETER;
            return nullptr;
        }
        std::unique_ptr<Net> result(new Net(node));
        node->remove_meta(reload_metadata); state.clear();
        if (error) *error = OK;
        return result;
    }
    Node *bridge() const { return id ? Object::cast_to<Node>(ObjectDB::get_instance(id)) : nullptr; }
    bool available() const { return bridge() != nullptr; }
    Ref<RefCounted> native_session() const {
        if (!available()) return {};
        Variant instance = bridge()->get("session"); Object *object = instance;
        return Ref<RefCounted>(Object::cast_to<RefCounted>(object));
    }
    void set_auto_poll(bool enabled) { if (auto *n = bridge()) n->set("auto_poll", enabled); }
    Error configure(const Options &options = {}) { return configure(options.dictionary()); }
    Error configure(const Dictionary &options) { return detail::error(bridge(), "configure", options); }
    Error host(int port = 10515, const String &bind = "0.0.0.0") { return detail::error(bridge(), "host", port, bind); }
    Error join_loopback(const String &address, int port = 10515) { return detail::error(bridge(), "join", address, port); }
    Error join_token(int64_t client, const PackedByteArray &token, const String &bind = "0.0.0.0") { return detail::error(bridge(), "join_token", client, token, bind); }
    TokenResult issue_token(int64_t client, const String &address) { return available() ? TokenResult::read(bridge()->call("issue_token", client, address)) : TokenResult{}; }
    Error poll() { return detail::error(bridge(), "poll"); }
    void stop() { if (auto *n = bridge()) n->call("stop"); }
    void close() { if (auto *n = bridge()) n->call("close"); }
    bool is_server() const { return available() && bool(bridge()->call("is_server")); }
    String state() const { return available() ? String(bridge()->call("get_state")) : String("Unconfigured"); }
    int tick_rate() const { return available() ? int64_t(bridge()->call("get_tick_rate")) : 0; }
    String simulation_fingerprint() const { return available() ? String(bridge()->call("get_simulation_fingerprint")) : String(); }
    Dictionary statistics() const { return available() ? Dictionary(bridge()->call("get_statistics")) : Dictionary(); }
    Array peers() const { return available() ? Array(bridge()->call("get_peers")) : Array(); }
    int64_t spawn(int kind, const Dictionary &state = {}, int64_t authority = -1) { return available() ? int64_t(bridge()->call("spawn", kind, state, authority)) : 0; }
    Error update_entity(int64_t entity, const Dictionary &state) { return detail::error(bridge(), "update_entity", entity, state); }
    Error despawn(int64_t entity) { return detail::error(bridge(), "despawn", entity); }
    Error set_entity_visible(int64_t entity, int64_t peer, bool visible) { return detail::error(bridge(), "set_entity_visible", entity, peer, visible); }
    Array entities() const { return available() ? Array(bridge()->call("get_entities")) : Array(); }
    Dictionary entity(int64_t handle) const { return available() ? Dictionary(bridge()->call("get_entity", handle)) : Dictionary(); }
    Error register_scene(int kind, const Ref<PackedScene> &scene, Node *parent) { return detail::error(bridge(), "register_scene", kind, scene, parent); }
    Error register_message(const StringName &name, const Callable &handler, Sender sender = Sender::Both) {
        Error result = detail::error(bridge(), "register_message", name, handler, int(sender));
        if (result == OK) messages.push_back(name);
        return result;
    }
    void unregister_message(const StringName &name) {
        if (auto *n = bridge()) n->call("unregister_message", name);
        messages.erase(std::remove(messages.begin(), messages.end(), name), messages.end());
    }
    Error send_message(int64_t peer, const StringName &name, const Array &arguments = {}) { return detail::error(bridge(), "send_message", peer, name, arguments); }
    Error broadcast_message(const StringName &name, const Array &arguments = {}) { return detail::error(bridge(), "broadcast_message", name, arguments); }
    Error send_input(int64_t entity, const Dictionary &input) { return detail::error(bridge(), "send_input", entity, input); }
    Error send_packet(int64_t peer, const PackedByteArray &data, int channel = 0, Delivery delivery = Delivery::ReliableOrdered) { return detail::error(bridge(), "send_packet", peer, data, channel, int(delivery)); }
    Error broadcast_packet(const PackedByteArray &data, int channel = 0, Delivery delivery = Delivery::ReliableOrdered) { return detail::error(bridge(), "broadcast_packet", data, channel, int(delivery)); }
    Error disconnect_peer(int64_t peer) { return detail::error(bridge(), "disconnect_peer", peer); }
    Error connect(const StringName &signal, const Callable &callback) {
        if (!available()) return ERR_UNCONFIGURED;
        auto result = bridge()->connect(signal, callback); if (result == OK) links.emplace_back(signal, callback); return result;
    }
    void disconnect(const StringName &signal, const Callable &callback) {
        if (auto *node = bridge(); node && node->is_connected(signal, callback)) node->disconnect(signal, callback);
        links.erase(std::remove_if(links.begin(), links.end(), [&](const auto &link) { return link.first == signal && link.second == callback; }), links.end());
    }
};

class Prediction {
    Ref<RefCounted> value;
public:
    Prediction() : value(detail::new_ref_script("res://addons/egp_net/egp_net_prediction.gd")) {}
    Prediction(const Prediction &) = delete;
    Prediction &operator=(const Prediction &) = delete;
    Ref<RefCounted> native() const { return value; }
    Error configure(const Callable &capture, const Callable &restore, const Callable &simulate,
        int64_t initial_tick = 0, int max_ticks = 128, int max_state_bytes = 65536, int max_history_bytes = 8388608) {
        return detail::error(value.ptr(), "configure", capture, restore, simulate, initial_tick, max_ticks, max_state_bytes, max_history_bytes);
    }
    Error predict(int64_t tick, const PackedByteArray &input) { return detail::error(value.ptr(), "predict", tick, input); }
    Error reconcile(int64_t ack, const PackedByteArray &state) { return detail::error(value.ptr(), "reconcile", ack, state); }
    Error reset(int64_t tick, const PackedByteArray &state) { return detail::error(value.ptr(), "reset", tick, state); }
    int pending_ticks() const { return value.is_valid() ? int64_t(value->call("get_pending_ticks")) : 0; }
    int history_bytes() const { return value.is_valid() ? int64_t(value->call("get_history_bytes")) : 0; }
};
class Box3D {
    Ref<RefCounted> value;
    std::vector<std::pair<StringName, Callable>> links;
    static constexpr const char *reload_metadata = "_egp_cpp_box3d_reload_token";
    explicit Box3D(const Ref<RefCounted> &adapter) : value(adapter) {}
    void disconnect_callbacks() {
        if (value.is_valid()) for (const auto &link : links) if (value->is_connected(link.first, link.second)) value->disconnect(link.first, link.second);
        links.clear();
    }
public:
    Box3D() : value(detail::new_ref_script("res://addons/egp_net/egp_net_box3d.gd")) {}
    ~Box3D() { disconnect_callbacks(); detach(); }
    Box3D(const Box3D &) = delete;
    Box3D &operator=(const Box3D &) = delete;
    bool available() const { return value.is_valid(); }
    Ref<RefCounted> native() const { return value; }
    /// Retain adapter/world/body mappings and the network clock link; release only this wrapper's callbacks and ownership.
    Dictionary detach_for_reload() {
        Dictionary state;
        if (!available()) return state;
        PackedByteArray token = detail::reload_token();
        if (token.size() != 32) return state;
        state["version"] = int64_t(1); state["adapter"] = value; state["token"] = token;
        value->set_meta(reload_metadata, token);
        disconnect_callbacks(); value.unref();
        return state;
    }
    static std::unique_ptr<Box3D> resume_after_reload(Dictionary &state, Error *error = nullptr) {
        auto *adapter = Object::cast_to<RefCounted>(detail::reload_object(state, "adapter", "res://addons/egp_net/egp_net_box3d.gd", reload_metadata));
        if (!adapter) { if (error) *error = ERR_INVALID_PARAMETER; return nullptr; }
        std::unique_ptr<Box3D> result(new Box3D(Ref<RefCounted>(adapter)));
        adapter->remove_meta(reload_metadata); state.clear();
        if (error) *error = OK;
        return result;
    }
    Error attach(Net &net, const Ref<RefCounted> &world) { return detail::error(value.ptr(), "attach", net.bridge(), world); }
    // A zero body_id keeps the entity-as-body convention; explicit IDs survive reconnect.
    Error track(int64_t entity, int64_t body_id = 0) { return detail::error(value.ptr(), "track", entity, body_id); }
    void untrack(int64_t entity) { if (value.is_valid()) value->call("untrack", entity); }
    void detach() { if (value.is_valid()) value->call("detach"); }
    Error connect(const StringName &signal, const Callable &callback) {
        if (!available()) return ERR_UNCONFIGURED;
        Error result = value->connect(signal, callback);
        if (result == OK) links.emplace_back(signal, callback);
        return result;
    }
    void disconnect(const StringName &signal, const Callable &callback) {
        if (available() && value->is_connected(signal, callback)) value->disconnect(signal, callback);
        links.erase(std::remove_if(links.begin(), links.end(), [&](const auto &link) { return link.first == signal && link.second == callback; }), links.end());
    }
};

/// Embed in a native actor and bind apply_network_state to apply().
/// Call process(delta) from the actor's _process. This only interpolates presentation.
class EntityPresentation3D {
    uint64_t id;
    Dictionary network_state;
    Transform3D target;
    bool has_target = false;
public:
    double smoothing_speed = 15.0;
    explicit EntityPresentation3D(Node3D &node) : id(node.get_instance_id()) {}
    Dictionary state() const { return network_state.duplicate(true); }
    void apply(const Dictionary &state) {
        network_state = state.duplicate(true);
        Node3D *node = Object::cast_to<Node3D>(ObjectDB::get_instance(id));
        if (node && state.get("transform", Variant()).get_type() == Variant::TRANSFORM3D) {
            target = state["transform"];
            if (!has_target || smoothing_speed <= 0) node->set_global_transform(target);
            has_target = true;
        }
    }
    void process(double delta) {
        Node3D *node = Object::cast_to<Node3D>(ObjectDB::get_instance(id));
        if (node && has_target) node->set_global_transform(smoothing_speed > 0
            ? node->get_global_transform().interpolate_with(target, 1.0 - std::exp(-smoothing_speed * delta)) : target);
    }
};
class EntityPresentation2D {
    uint64_t id;
    Dictionary network_state;
    Transform2D target;
    bool has_target = false;
public:
    double smoothing_speed = 15.0;
    explicit EntityPresentation2D(Node2D &node) : id(node.get_instance_id()) {}
    Dictionary state() const { return network_state.duplicate(true); }
    void apply(const Dictionary &state) {
        network_state = state.duplicate(true);
        Node2D *node = Object::cast_to<Node2D>(ObjectDB::get_instance(id));
        if (node && state.get("transform", Variant()).get_type() == Variant::TRANSFORM2D) {
            target = state["transform"];
            if (!has_target || smoothing_speed <= 0) node->set_global_transform(target);
            has_target = true;
        }
    }
    void process(double delta) {
        Node2D *node = Object::cast_to<Node2D>(ObjectDB::get_instance(id));
        if (node && has_target) node->set_global_transform(smoothing_speed > 0
            ? node->get_global_transform().interpolate_with(target, 1.0 - std::exp(-smoothing_speed * delta)) : target);
    }
};
}
