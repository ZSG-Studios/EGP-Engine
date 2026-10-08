extends Node
# Real native server + clients. Deltas stay inside the native transport;
# every exposed entity.state remains a complete validated property frame.
class Actor extends Node3D:
 @export var points: int = 0
 @export var uv: Vector2 = Vector2.ZERO
 @export var tint: Color = Color.WHITE
 @export var caption: String = "Stable schema and unchanged properties should not be resent on every gameplay update."

var checks := 0
var failed := false
var server := EGPNetSession.new()
var client := EGPNetSession.new()
var late := EGPNetSession.new()
var late_connected := false
var source_actor: Actor
var client_actor: Actor
var late_actor: Actor
var source: Superposition
var replica: Superposition
var late_replica: Superposition
var pump_diagnostics: Dictionary = {}
var fixture_started_ms := 0
var fixture_deadline_ms := 0

func session_diagnostics(session: EGPNetSession) -> Dictionary:
 var result := {"state": session.get_state(), "statistics": session.get_statistics()}
 var entities = session.command("entities")
 if entities is Array:
  result["entities"] = []
  for entity in entities:
   result["entities"].append({"entity": entity.entity, "revision": entity.revision, "state_bytes": entity.state.size()})
 var peers = session.command("peers")
 if peers is Array:
  result["peers"] = []
  for peer in peers:
   result["peers"].append({"peer_id": peer.peer_id, "replication": session.command("replication_peer_statistics", {"peer": peer.peer_id})})
 return result

func check(value: bool, message: String) -> void:
 if not value or (fixture_deadline_ms > 0 and Time.get_ticks_msec() >= fixture_deadline_ms):
  if not failed:
   var context := {"first_failure": message, "checks_before_failure": checks, "pump": pump_diagnostics,
    "fixture_elapsed_ms": Time.get_ticks_msec() - fixture_started_ms, "fixture_deadline_ms": fixture_deadline_ms,
    "fixture_deadline_exceeded": Time.get_ticks_msec() >= fixture_deadline_ms,
    "server": session_diagnostics(server), "client": session_diagnostics(client), "late": session_diagnostics(late)}
   if is_instance_valid(source_actor): context["source_actor"] = {"points": source_actor.points, "tint": source_actor.tint}
   if is_instance_valid(client_actor): context["client_actor"] = {"points": client_actor.points, "tint": client_actor.tint}
   if is_instance_valid(late_actor): context["late_actor"] = {"points": late_actor.points, "tint": late_actor.tint}
   if is_instance_valid(source): context["source_component"] = source.get_statistics()
   if is_instance_valid(replica): context["client_component"] = replica.get_statistics()
   if is_instance_valid(late_replica): context["late_component"] = late_replica.get_statistics()
   print("SUPERPOSITION_DELTA_FIRST_FAILURE ", JSON.stringify(context))
  failed = true
  push_error(message)
  get_tree().quit(1)
 else:
  checks += 1

func attach(actor: Actor, session: EGPNetSession) -> Superposition:
 add_child(actor)
 var component := Superposition.new()
 component.enabled = false
 component.replication_key = "delta-fixture-actor"
 actor.add_child(component)
 # The same setters back the Inspector checkboxes: types are inferred.
 for property in ["points", "uv", "tint", "caption"]:
  component.set("replicate/" + property, true)
 component.config.delta_replication = true
 component.config.update_rate = 30
 component.set_session(session)
 return component

func pump(seconds: float, expected: Callable = Callable()) -> void:
 var started := Time.get_ticks_msec()
 var previous := started
 var deadline := started + int(seconds * 1000)
 pump_diagnostics = {"requested_ms": int(seconds * 1000), "frames": 0, "max_frame_gap_ms": 0, "elapsed_ms": 0,
  "completion_wait_ms": 0, "fixture_deadline_ms": fixture_deadline_ms, "completion_required": expected.is_valid()}
 while not failed and Time.get_ticks_msec() < fixture_deadline_ms and (Time.get_ticks_msec() < deadline or (expected.is_valid() and not expected.call())):
  var current := Time.get_ticks_msec()
  pump_diagnostics.max_frame_gap_ms = maxi(pump_diagnostics.max_frame_gap_ms, current - previous)
  pump_diagnostics.frames += 1
  pump_diagnostics.elapsed_ms = current - started
  previous = current
  check(server.poll() == OK and client.poll() == OK, "Poll native sessions")
  if late_connected: check(late.poll() == OK, "Poll late session")
  if server.get_state() == "Listening": pump_diagnostics.source_result = source.replicate_now()
  if client.get_state() == "Connected": pump_diagnostics.client_result = replica.replicate_now()
  if late_connected and late.get_state() == "Connected": pump_diagnostics.late_result = late_replica.replicate_now()
  await get_tree().process_frame
 pump_diagnostics.elapsed_ms = Time.get_ticks_msec() - started
 pump_diagnostics.completion_wait_ms = maxi(0, pump_diagnostics.elapsed_ms - pump_diagnostics.requested_ms)
 pump_diagnostics.expected_completed = not expected.is_valid() or expected.call()
 pump_diagnostics.deadline_exceeded = Time.get_ticks_msec() >= fixture_deadline_ms

func check_session_binding() -> void:
 var world := SuperpositionWorld.new()
 world.auto_poll = false
 add_child(world)
 check(world.configure() == OK, "Configure automatic World provider")
 var actor := Actor.new()
 world.add_child(actor)
 var component := Superposition.new()
 actor.add_child(component)
 component.set("replicate/points", true)
 await get_tree().process_frame
 await get_tree().process_frame
 check(component.get_session() == world.get_session(), "Discover ancestor World automatically")
 var override_session := EGPNetSession.new()
 check(override_session.configure() == OK, "Configure explicit override")
 component.set_session(override_session)
 component.replication_key = "manual-schema-reset"
 await get_tree().process_frame
 check(component.get_session() == override_session, "Manual override survives provider polling and binding reset")
 component.set_session(null)
 await get_tree().process_frame
 check(component.get_session() == world.get_session(), "Clearing explicit ref resumes automatic discovery")
 component.set("replicate/uv", true)
 component.replicate_now()
 var original := world.get_session()
 world.stop()
 check(world.configure() == OK, "Replace automatic World after schema reset")
 await get_tree().process_frame
 check(component.get_session() == world.get_session() and component.get_session() != original, "Schema reset preserves automatic rebinding mode")
 component.set_session(world.get_session())
 world.stop()
 check(world.configure() == OK, "Replace World after explicit equal-ref assignment")
 await get_tree().process_frame
 check(component.get_session() != world.get_session(), "Explicit equal-ref assignment becomes sticky")
 component.session_path = NodePath("../..")
 await get_tree().process_frame
 check(component.get_session() == world.get_session(), "Changing provider path clears explicit binding")
 world.queue_free()
 await get_tree().process_frame

func _ready() -> void:
 fixture_started_ms = Time.get_ticks_msec()
 fixture_deadline_ms = fixture_started_ms + 28000
 await check_session_binding()
 var options := {"allow_insecure_loopback": true, "game_protocol": "superposition-delta-fixture-v1", "max_entities": 8, "simulated_latency_ms": 35.0, "simulated_loss": 8.0, "simulated_jitter_ms": 10.0}
 check(server.configure(options) == OK and client.configure(options) == OK and late.configure(options) == OK, "Configure impairment sessions")
 source_actor = Actor.new()
 client_actor = Actor.new()
 late_actor = Actor.new()
 source = attach(source_actor, server)
 replica = attach(client_actor, client)
 late_replica = attach(late_actor, late)
 check(source.config.properties.size() == 4, "Checkboxes create four inferred rules")
 check(source.config.properties[1].value_type == TYPE_VECTOR2 and source.config.properties[2].value_type == TYPE_COLOR, "Vector2 and Color Inspector types inferred")
 check(server.listen(0, "127.0.0.1") == OK, "Listen")
 check(source.replicate_now() == OK, "Spawn one native gameplay entity")
 check(server.command("entities").size() == 1, "Delta mode uses one entity, no companion")
 var port: int = server.get_statistics().local_port
 check(client.connect_to_server("127.0.0.1", port) == OK, "Connect")
 await pump(0.8, func(): return client.get_state() == "Connected" and client_actor.caption == source_actor.caption)
 check(client.get_state() == "Connected" and client_actor.caption == source_actor.caption, "Full initial schema baseline")
 var peer: int = server.command("peers")[0].peer_id
 check(client.command("set_entity_delta_replication", {"entity": source.get_statistics().entity, "enabled": true}) == ERR_UNAUTHORIZED, "Client cannot set delta policy")
 check(server.command("set_entity_delta_replication", {"entity": source.get_statistics().entity, "enabled": 1}) == ERR_INVALID_PARAMETER, "Delta checkbox dispatcher requires bool")
 for round_index in range(4):
  for skipped_revision in range(20):
   source_actor.points = round_index * 20 + skipped_revision
   check(source.replicate_now() == OK, "Capture latest coalesced property state")
  await pump(0.45, func(): return client_actor.points == source_actor.points)
  check(client_actor.points == source_actor.points, "ACK-based delta survives coalesced revisions and impairment")
 var statistics: Dictionary = server.command("replication_peer_statistics", {"peer": peer})
 check(statistics.delta_updates > 0 and statistics.delta_bytes_saved > 0, "Native delta bytes savings observable")
 check(statistics.baseline_bytes <= 1024 * 1024, "Retained snapshots bounded")
 check(client.command("entities").size() == 1, "Receiver retains one complete reconstructed entity")
 source_actor.uv = Vector2(1.24, 2.26)
 source_actor.tint = Color(0.126, 0.274, 0.526, 1.0)
 check(source.replicate_now() == OK, "Capture finite Vector2 and Color")
 await pump(0.45, func(): return client_actor.uv.is_equal_approx(Vector2(1.24, 2.26)) and client_actor.tint.is_equal_approx(Color(0.13, 0.27, 0.53, 1.0)))
 check(client_actor.uv.is_equal_approx(Vector2(1.24, 2.26)), "Vector2 applies quantized values")
 check(client_actor.tint.is_equal_approx(Color(0.13, 0.27, 0.53, 1.0)), "Color applies component quantization")
 var malformed: Array = bytes_to_var(source.capture_state())
 var before := client_actor.points
 malformed[2][2][3] = Color(NAN, 1, 1)
 check(replica.apply_state(var_to_bytes(malformed)) == ERR_INVALID_DATA and client_actor.points == before, "Malformed Color rejects atomically")
 source_actor.uv = Vector2(INF, 0)
 check(source.capture_state().is_empty(), "Non-finite Vector2 rejects before wire encoding")
 source_actor.uv = Vector2(1.24, 2.26)
 source.config.interest_radius = 4.0
 source.set_observer_position(peer, Vector3(100, 0, 0))
 await pump(0.4, func(): return client.command("entities").is_empty())
 check(client.command("entities").is_empty(), "Interest exit tears down delta baseline")
 source_actor.points = 300
 source.set_observer_position(peer, Vector3.ZERO)
 await pump(0.5, func(): return client_actor.points == 300 and client.command("entities").size() == 1)
 check(client_actor.points == 300 and client.command("entities").size() == 1, "Re-entry receives latest complete baseline")
 late_connected = true
 check(late.connect_to_server("127.0.0.1", port) == OK, "Connect late peer")
 await pump(0.7, func(): return late.get_state() == "Connected" and late_actor.points == 300 and late_actor.tint.is_equal_approx(client_actor.tint) and late_actor.uv.is_equal_approx(client_actor.uv) and late_actor.caption == source_actor.caption)
 check(late.get_state() == "Connected" and late_actor.points == 300 and late_actor.tint.is_equal_approx(client_actor.tint), "Late join reconstructs all properties")
 source.config.delta_replication = false
 check(source.replicate_now() == OK, "Disable opt-in policy")
 check(server.command("replication_peer_statistics", {"peer": peer}).baseline_bytes == 0, "Disable frees retained baseline memory")
 source_actor.points = 500
 await pump(0.45, func(): return client_actor.points == 500 and late_actor.points == 500)
 check(client_actor.points == 500 and late_actor.points == 500, "Default full-state mode remains compatible")
 source.set_session(null)
 await pump(0.35, func(): return server.command("entities").is_empty() and client.command("entities").is_empty())
 check(server.command("entities").is_empty() and client.command("entities").is_empty(), "Rebind retires one entity safely")
 client.stop()
 late.stop()
 server.stop()
 if not failed:
  print("SUPERPOSITION_DELTA_CHECKS_PASS ", checks)
  get_tree().quit(0)
