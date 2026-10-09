// SPDX-License-Identifier: MIT
// Included only by the opt-in native lifecycle fixture. Production excludes it.
#include "superpos_spawner.h"
#include "scene/resources/packed_scene.h"
#include "scene/main/window.h"
namespace superpos_egp::spawning::fixture {
using lifecycle_engine::fixture::checks;
unsigned constructed{}, destroyed{}, values_set{};
bool fail_second{}, close_value{}, close_construct{}, stop_value{};
ObjectID close_session;
SuperposSpawner *stop_spawner{};
Error stop_result = FAILED;
class SuperposProjectionFixtureNode final : public Node {
    GDCLASS(SuperposProjectionFixtureNode, Node);
    int value_{}, second_{};
protected:
    static void _bind_methods() {}
    bool _set(const StringName &name, const Variant &value) {
        if (name == StringName("value")) {
            value_ = int(value); ++values_set;
            if (close_value && value_ == 42) {
                close_value = false;
                if (auto *session = Object::cast_to<SuperposSession>(ObjectDB::get_instance(close_session)))
                    (void)session->close_checked();
            }
            if (stop_value && value_ == 42) {
                // Cancels the receiver from inside the root's own setter. The
                // runtime must invalidate the ticket and defer deleting this node.
                stop_value = false;
                stop_result = stop_spawner ? stop_spawner->stop() : FAILED;
            }
            return true;
        }
        if (name == StringName("second")) {
            if (fail_second) { fail_second = false; return false; }
            second_ = int(value); return true;
        }
        return false;
    }
    bool _get(const StringName &name, Variant &value) const {
        if (name == StringName("value")) { value = value_; return true; }
        if (name == StringName("second")) { value = second_; return true; }
        return false;
    }
public:
    SuperposProjectionFixtureNode() {
        ++constructed;
        if (close_construct) {
            close_construct = false;
            if (auto *session = Object::cast_to<SuperposSession>(ObjectDB::get_instance(close_session))) (void)session->close_checked();
        }
    }
    ~SuperposProjectionFixtureNode() { ++destroyed; }
};
struct Run {
    std::array<lifecycle_engine::fixture::Carrier, 2> carriers;
    std::array<Ref<SuperposSession>, 2> sessions;
    Ref<SuperposReplicaView> old_view;
    Ref<SuperposSpawnCatalog> catalog;
    SuperposSpawner *spawner{};
    unsigned scenario{}, stage{}, baseline_nodes{}, baseline_live{}, wait_frames{}, timed_out{};
    bool failed{};
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    size_t baseline{};
    bool create() {
        baseline = module_backing().total();
        carriers[0].remote = &carriers[1]; carriers[1].remote = &carriers[0];
        Ref<SuperposSchema> schema; schema.instantiate(); schema->set_schema_id(19);
        TypedArray<SuperposField> fields;
        for (unsigned id = 1; id <= 2; ++id) { Ref<SuperposField> field; field.instantiate(); field->set_field_id(id); field->set_codec_id(4); field->set_max_bytes(4); fields.append(field); }
        schema->set_fields(fields); TypedArray<SuperposSchema> schemas; schemas.append(schema);
        for (unsigned i = 0; i < 2; ++i) {
            sessions[i].instantiate(); VERIFY(sessions[i]->configure(schemas, 4, 7, 1, 1 << 20) == OK);
            superpos::SessionConfig config; config.session_id = 100 + scenario; config.epoch = 8;
            config.local_peer = i + 1; config.remote_peer = 2 - i; config.logical_channels = 3;
            config.channel_modes[0] = superpos::DeliveryMode::ReliableOrdered;
            config.channel_modes[1] = config.channel_modes[2] = superpos::DeliveryMode::ReliableUnordered;
            config.channel_purposes[0] = superpos::ChannelPurpose::Control;
            VERIFY(SuperposNativeReceiverAccess::bind_fixture_transport(*sessions[i].ptr(), carriers[i], config) == OK);
        }
        auto *template_node = memnew(SuperposProjectionFixtureNode);
        Ref<PackedScene> scene; scene.instantiate(); VERIFY(scene->pack(template_node) == OK); memdelete(template_node);
        Ref<SuperposSpawnEntry> entry; entry.instantiate(); entry->set_schema_id(19); entry->set_resource_id(101); entry->set_scene(scene);
        Dictionary mappings;
        for (int64_t id = 1; id <= 2; ++id) { Dictionary mapping; mapping["node"] = NodePath(); mapping["property"] = id == 1 ? "value" : "second"; mappings[id] = mapping; }
        entry->set_fields(mappings); catalog.instantiate(); TypedArray<SuperposSpawnEntry> entries; entries.append(entry); catalog->set_entries(entries);
        entry->set_resource_id(0); VERIFY(int(catalog->validate()["error"]) == ERR_INVALID_PARAMETER);
        entry->set_resource_id(101); VERIFY(int(catalog->validate()["error"]) == OK);
        baseline_nodes = constructed; baseline_live = constructed - destroyed;
        spawner = memnew(SuperposSpawner); spawner->set_session(sessions[1]); spawner->set_catalog(catalog);
        SceneTree::get_singleton()->get_root()->add_child(spawner);
        return true;
    }
    Dictionary configuration() {
        Dictionary config; config["replica_epoch"] = 9; config["maximum_active"] = 4; config["maximum_transitions"] = 4; config["native_bytes"] = 1 << 20;
        if (scenario == 6) config["spawn_timeout_ms"] = 100;
        return config;
    }
    bool send(superpos::ReplicaWireMessage message, unsigned channel) {
        message.context = {7, 8, 9}; std::array<std::byte, 8192> bytes;
        auto encoded = superpos::encode_replica_message(message, bytes); VERIFY(encoded);
        PackedByteArray packet; VERIFY(packet.resize(*encoded) == OK); std::memcpy(packet.ptrw(), bytes.data(), *encoded);
        VERIFY(int(sessions[0]->enqueue_packet(packet, channel)["error"]) == OK); return true;
    }
    bool offer_state() {
        const superpos::ReplicaKey key{1, 1, 7, 8, 9, 1};
        std::array<std::byte, 8> bytes{}; bytes[0] = std::byte{42}; bytes[4] = std::byte{52};
        superpos::ReplicaWireMessage message; message.kind = superpos::ReplicaWireKind::BaselineOffer; message.sequence = 1;
        message.baseline = {key, 0, 1, 1, bytes}; message.tick = 1; VERIFY(send(message, 2));
        return true;
    }
    bool bind(bool state = true) {
        const superpos::ReplicaKey key{1, 1, 7, 8, 9, 1};
        superpos::ReplicaWireMessage message; message.kind = superpos::ReplicaWireKind::Bind;
        message.binding = {key, superpos::ObjectHandle::from_parts(70, 1), 19, 2, 1, 1}; VERIFY(send(message, 0));
        if (state) VERIFY(offer_state());
        return true;
    }
    bool cleanup() {
        if (spawner) { const Error stopped = spawner->stop(); VERIFY(stopped == OK || stopped == ERR_BUSY); memdelete(spawner); spawner = nullptr; }
        for (auto &session : sessions) if (session.is_valid()) { const auto closed = session->close_checked(); VERIFY(closed == OK || closed == ERR_BUSY); }
        SuperposNativeReceiverAccess::drain();
        for (auto &session : sessions) session.unref(); old_view.unref(); catalog.unref();
        SuperposNativeReceiverAccess::drain(); VERIFY(module_backing().total() == baseline);
        VERIFY(constructed - destroyed == baseline_live);
        stop_spawner = nullptr; stop_value = false;
        for (auto &carrier : carriers) carrier.head = carrier.count = 0;
        return true;
    }
    bool step() {
        if (failed) return false;
        VERIFY(std::chrono::steady_clock::now() < deadline);
        if (!stage) { VERIFY(create()); stage = 1; }
        for (unsigned peer = 0; peer < sessions.size(); ++peer) if (sessions[peer].is_valid()) {
            const Error advanced = sessions[peer]->advance_tick();
            if (advanced != OK && advanced != ERR_BUSY) {
                std::printf("PUBLIC_SPAWNER_PUMP scenario=%u stage=%u peer=%u error=%d last=%d state=%s\n", scenario, stage, peer, int(advanced), int(sessions[peer]->get_last_error()), sessions[peer]->get_state().utf8().get_data());
                std::fflush(stdout);
            }
            VERIFY(advanced == OK || advanced == ERR_BUSY);
        }
        if (stage == 1) {
            if (sessions[0]->get_state() != String("NetworkReady") || sessions[1]->get_state() != String("NetworkReady")) return false;
            Dictionary invalid = configuration(); invalid["resource_id"] = 999; VERIFY(spawner->start(invalid) == ERR_INVALID_PARAMETER);
            invalid = configuration(); invalid["atomic_scene"] = 1; VERIFY(spawner->start(invalid) == ERR_INVALID_PARAMETER);
            invalid = configuration(); invalid["spawn_timeout_ms"] = 0; VERIFY(spawner->start(invalid) == ERR_INVALID_PARAMETER);
            invalid = configuration(); invalid["spawn_timeout_ms"] = 60001; VERIFY(spawner->start(invalid) == ERR_INVALID_PARAMETER);
            VERIFY(spawner->start(configuration()) == OK); spawner->set_process(false);
            VERIFY(int(sessions[1]->read_packet(0)["error"]) == ERR_BUSY);
            VERIFY(int(spawner->read_status()["spawn_timeout_ms"]) == (scenario == 6 ? 100 : 15000));
            VERIFY(bind(scenario != 2 && scenario != 6)); stage = 2;
        }
        if (stage == 2 && scenario == 6) {
            // Readiness deadline: the bound replica never receives its initial
            // state in time. Expiry is typed metadata; no scene is constructed.
            const Dictionary progress = spawner->project_pending(); VERIFY(int(progress["error"]) == OK);
            timed_out += unsigned(int(progress["timed_out"]));
            if (int(spawner->read_status()["spawn_timed_out"]) == 0) return false;
            VERIFY(timed_out == 1 && int(spawner->read_status()["spawn_timed_out"]) == 1);
            VERIFY(int(spawner->project_pending()["timed_out"]) == 0);
            VERIFY(constructed == baseline_nodes && spawner->get_child_count() == 0 && spawner->get_replicas().is_empty());
            VERIFY(offer_state()); stage = 7; return false;
        }
        if (stage == 7) {
            auto views = spawner->get_replicas(); if (views.is_empty()) return false;
            Ref<SuperposReplicaView> late = views[0];
            Dictionary status = late->read_status(); VERIFY(int(status["error"]) == OK && bool(status["canonical_ready"]));
            Dictionary scene = status["scene"];
            VERIFY(String(scene["scene_phase"]) == String("failed") && bool(scene["spawn_timed_out"]) &&
                    unsigned(int(scene["projection_error"])) == unsigned(superpos::Error::Timeout) && int(scene["completed_fields"]) == 0);
            // A late initial state is canonical but stays withheld from the scene.
            VERIFY(int(spawner->project_pending()["completed"]) == 0 && spawner->get_child_count() == 0 && constructed == baseline_nodes);
            PackedInt64Array ids; ids.append(1); ids.append(2); const Dictionary image = late->read_fields(ids);
            VERIFY(int(image["error"]) == OK); const Dictionary values = image["values"]; VERIFY(int(values[int64_t(1)]) == 42 && int(values[int64_t(2)]) == 52);
            VERIFY(late->retry_projection() == OK);
            VERIFY(int(spawner->project_pending()["completed"]) == 1);
            status = late->read_status(); scene = status["scene"];
            VERIFY(String(scene["scene_phase"]) == String("ready") && !bool(scene["spawn_timed_out"]) && int(scene["completed_fields"]) == 2);
            VERIFY(spawner->get_child_count() == 1 && int(spawner->get_child(0)->get("value")) == 42 && int(spawner->get_child(0)->get("second")) == 52);
            VERIFY(int(spawner->read_status()["spawn_timed_out"]) == 0);
            VERIFY(spawner->stop() == OK && spawner->get_child_count() == 0 && int(late->read_status()["error"]) != OK);
            stage = 6;
        }
        if (stage == 2) {
            if (scenario == 2) {
                if (++wait_frames < 3) return false;
                VERIFY(constructed == baseline_nodes && spawner->get_child_count() == 0);
                superpos::ReplicaWireMessage leave; leave.kind = superpos::ReplicaWireKind::Leave; leave.key = {1,1,7,8,9,1}; leave.sequence = 2;
                VERIFY(send(leave, 0)); stage = 5; wait_frames = 0; return false;
            }
            auto views = spawner->get_replicas(); if (views.is_empty()) return false;
            old_view = views[0]; VERIFY(old_view.is_valid());
            auto before = old_view->read_status(); VERIFY(int(before["error"]) == OK && bool(before["canonical_ready"]));
            VERIFY(constructed == baseline_nodes && spawner->get_child_count() == 0);
            Dictionary scene = before["scene"]; VERIFY(String(scene["scene_phase"]) == String("pending"));
            fail_second = scenario == 1; close_value = scenario == 3; close_construct = scenario == 4; close_session = sessions[1]->get_instance_id();
            stop_value = scenario == 5; stop_spawner = spawner; stop_result = FAILED;
            VERIFY(int(spawner->project_pending()["error"]) == OK);
            if (scenario == 5) {
                // Reentrant cancellation completed synchronously: no scene node,
                // stale views, detached receiver and no live constructed root.
                VERIFY(stop_result == OK && !stop_value);
                VERIFY(spawner->get_child_count() == 0 && constructed == baseline_nodes + 1 && constructed - destroyed == baseline_live);
                VERIFY(int(old_view->read_status()["error"]) != OK && old_view->retry_projection() != OK);
                VERIFY(int(spawner->read_status()["error"]) == ERR_UNCONFIGURED && !sessions[1]->read_receiver_status().has("attached"));
                VERIFY(spawner->get_replicas().is_empty() && int(spawner->project_pending()["error"]) == ERR_UNCONFIGURED);
                stage = 6;
            }
            else if (scenario == 3 || scenario == 4) { SuperposNativeReceiverAccess::drain(); VERIFY(int(old_view->read_status()["error"]) != OK); stage = 6; }
            else {
                auto after = old_view->read_status(); scene = after["scene"];
                if (scenario == 1) {
                    VERIFY(String(scene["scene_phase"]) == String("failed") && int(scene["completed_fields"]) == 1);
                    VERIFY(spawner->get_child_count() == 0); VERIFY(old_view->retry_projection() == OK);
                    VERIFY(int(spawner->project_pending()["error"]) == OK); after = old_view->read_status(); scene = after["scene"];
                }
                VERIFY(String(scene["scene_phase"]) == String("ready") && int(scene["completed_fields"]) == 2);
                VERIFY(spawner->get_child_count() == 1); VERIFY(int(spawner->get_child(0)->get("value")) == 42 && int(spawner->get_child(0)->get("second")) == 52);
                PackedInt64Array ids; ids.append(1); ids.append(2); const Dictionary image = old_view->read_fields(ids);
                VERIFY(int(image["error"]) == OK); const Dictionary values = image["values"]; VERIFY(int(values[int64_t(1)]) == 42 && int(values[int64_t(2)]) == 52);
                VERIFY(old_view->retry_projection() == ERR_BUSY);
                VERIFY(spawner->stop() == OK); VERIFY(int(old_view->read_status()["error"]) != OK);
                if (scenario == 0) {
                    VERIFY(spawner->start(configuration()) == OK); spawner->set_process(false);
                    VERIFY(bind()); stage = 4; return false;
                }
                stage = 6;
            }
        }
        if (stage == 4) {
            auto views = spawner->get_replicas(); if (views.is_empty()) return false;
            Ref<SuperposReplicaView> fresh = views[0]; VERIFY(fresh->get_binding_generation() != old_view->get_binding_generation());
            VERIFY(int(old_view->read_status()["error"]) != OK && old_view->retry_projection() != OK);
            VERIFY(int(fresh->read_status()["error"]) == OK); stage = 6;
        }
        if (stage == 5) {
            if (++wait_frames < 3) return false;
            VERIFY(spawner->get_replicas().is_empty() && spawner->get_child_count() == 0 && constructed == baseline_nodes); stage = 6;
        }
        if (stage == 6) { VERIFY(cleanup()); stage = 0; wait_frames = 0; timed_out = 0; if (++scenario == 7) return true; }
        return false;
    }
};
Run *run{};
// Idle callbacks run from both physics and process frames. After a physics-frame
// quit, the same iteration's process frame must not construct a fresh Run.
bool finished{};
void idle() {
    if (finished) return;
    if (!run) run = memnew(Run);
    if (run->step()) { finished = true; memdelete(run); run = nullptr;
        std::printf("EGP_PUBLIC_SPAWNER_OK checks=%u cases=7 canonical_before_scene=1 retry=1 stale_rebind=1 destroy_pending=1 construction_reentry=1 setter_reentry=1 stop_reentry=1 spawn_timeout=1 mock_carrier=1\n", lifecycle_engine::fixture::checks);
        std::fflush(stdout); SceneTree::get_singleton()->quit(0); }
}
}
void superpos_register_public_spawner_fixture() {
    for (const String &arg : OS::get_singleton()->get_cmdline_user_args()) if (arg == "--superpos-public-spawner-fixture") {
        GDREGISTER_CLASS(superpos_egp::spawning::fixture::SuperposProjectionFixtureNode);
        SceneTree::add_idle_callback(superpos_egp::spawning::fixture::idle); return;
    }
}
