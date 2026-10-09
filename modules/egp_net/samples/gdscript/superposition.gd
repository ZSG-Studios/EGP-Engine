extends Node
# Two real native Yojimbo sessions in one process. No mocked transport.
var server: EGPNetSession
var client: EGPNetSession
var late: EGPNetSession
var checks := 0
var failed := false
var server_actor: Node3D
var client_actor: Node3D
var source: Superposition
var replica: Superposition
func check(condition: bool, message: String) -> void:
 if not condition:
  failed = true
  push_error(message)
  get_tree().quit(1)
  return
 checks += 1
func pump(duration: float) -> void:
 var until := Time.get_ticks_msec() + int(duration * 1000)
 while Time.get_ticks_msec() < until:
  server.poll()
  client.poll()
  if late != null: late.poll()
  replica.replicate_now()
  await get_tree().process_frame
func _ready() -> void:
 server_actor = $ServerActor
 client_actor = $ClientActor
 source = $ServerActor/Replication
 replica = $ClientActor/Replication
 server = EGPNetSession.new()
 client = EGPNetSession.new()
 var options := {"allow_insecure_loopback": true, "game_protocol": "superposition-fixture-v1", "max_entities": 32}
 check(server.configure(options) == OK and client.configure(options) == OK, "Configure native sessions")
 check(server.listen(0, "127.0.0.1") == OK, "Listen native session")
 source.set_session(server)
 replica.set_session(client)
 server_actor.position = Vector3(1.24, 2.26, 3.26)
 var duplicate := Superposition.new()
 duplicate.enabled = false
 duplicate.config = source.config
 duplicate.replication_key = source.replication_key
 server_actor.add_child(duplicate)
 duplicate.set_session(server)
 var payload := source.capture_state()
 check(not payload.is_empty() and payload.size() <= 4096, "Bounded quantized capture")
 check(source.apply_state(payload) == ERR_UNAUTHORIZED, "Server rejects incoming property state")
 check(source.replicate_now() == OK, "Server spawns native entity")
 check(duplicate.replicate_now() == ERR_ALREADY_EXISTS, "Duplicate replication identity rejects")
 duplicate.queue_free()
 var port: int = server.get_statistics().local_port
 check(client.connect_to_server("127.0.0.1", port) == OK, "Connect native client")
 await pump(0.6)
 check(client.get_state() == "Connected", "Native client connected")
 check(client_actor.position.is_equal_approx(Vector3(1.2, 2.3, 3.3)), "Late join receives quantized entity baseline")
 var sends: int = source.get_statistics().sent
 for i in range(100): source.replicate_now()
 check(source.get_statistics().sent == sends and source.get_statistics().dirty_skips >= 100, "Unchanged quantized state sleeps without updates")
 server_actor.position = Vector3(1.21, 2.31, 3.31)
 source.replicate_now()
 check(source.get_statistics().sent == sends, "Sub-quantum movement remains dormant")
 server_actor.position = Vector3(2, 3, 4)
 check(source.replicate_now() == OK, "Dirty state sends update")
 await pump(0.4)
 check(client_actor.position.is_equal_approx(Vector3(2, 3, 4)), "Native update applies on client")
 var before := client_actor.position
 check(replica.get("status/statistics").has("last_error"), "Inspector exposes status dictionary")
 var rule: SuperpositionProperty = source.config.properties[0]
 rule.quantization = 1e-300
 check(source.capture_state().is_empty(), "Underflowing quantization rejects before encoding")
 rule.quantization = 0.1
 var malformed: Array = bytes_to_var(payload)
 malformed[2][1][0] = "name"
 check(replica.apply_state(var_to_bytes(malformed)) == ERR_INVALID_DATA and client_actor.position == before, "Mismatched property rejects without mutation")
 malformed = bytes_to_var(payload)
 malformed[2][0][3] = Vector3(NAN, 0, 0)
 check(replica.apply_state(var_to_bytes(malformed)) == ERR_INVALID_DATA and client_actor.position == before, "Non-finite state rejects without mutation")
 check(replica.apply_state(payload + PackedByteArray([0])) == ERR_INVALID_DATA, "Trailing data rejects")
 var peers: Array = server.command("peers")
 check(peers.size() == 1, "One independent native peer")
 var peer: int = peers[0].peer_id
 check(source.set_observer_position(peer, Vector3(100, 0, 0)) == OK, "Set interest observer")
 source.replicate_now()
 await pump(0.4)
 check(client.command("entities").is_empty(), "Out-of-radius native entity hidden")
 source.set_observer_position(peer, Vector3(2, 3, 4))
 source.replicate_now()
 await pump(0.4)
 check(client.command("entities").size() == 1 and client_actor.position == before, "Interest re-entry gets current baseline")
 late = EGPNetSession.new()
 check(late.configure(options) == OK and late.connect_to_server("127.0.0.1", port) == OK, "Connect second late native client")
 await pump(0.5)
 check(late.get_state() == "Connected" and late.command("entities").size() == 1, "Second late join sees dormant current state")
 check(source.get_statistics().sent == sends + 1, "Only actual changed quantized state sent")
 check(replica.get_statistics().rejected == 3 and replica.get_statistics().applied >= 2, "Observable acceptance/rejection statistics")
 source.config.priority = 8
 check(source.replicate_now() == OK and source.get_statistics().priority == 8, "Inspector priority applies native scheduling policy")
 check(server.command("set_replication_priority", {"entity": source.get_statistics().entity, "priority": "fast"}) == ERR_INVALID_PARAMETER, "Native dispatcher rejects untyped priority")
 check(client.command("set_peer_replication_budget", {"peer": 0, "bytes_per_second": 128}) == ERR_UNAUTHORIZED, "Client cannot configure server replication budget")
 check(server.command("set_peer_replication_budget", {"peer": peer, "bytes_per_second": 128}) == OK, "Peer update byte policy accepts a finite small budget")
 check(server.command("replication_peer_statistics", {"peer": peer}).bytes_per_second == 128, "Peer update budget is observable")
 check(server.command("set_peer_replication_budget", {"peer": peer, "bytes_per_second": 0}) == OK, "Zero disables gameplay update subbudget")
 source.set_observer_position(peer, server_actor.position + Vector3(9, 0, 0))
 source.replicate_now()
 await pump(0.15)
 check(client.command("entities").size() == 1, "Hysteresis keeps visible entity near interest boundary")
 source.set_observer_position(peer, server_actor.position + Vector3(11, 0, 0))
 source.replicate_now()
 await pump(0.15)
 check(client.command("entities").is_empty(), "Leaving radius plus hysteresis hides entity")
 source.set_observer_position(peer, server_actor.position + Vector3(9, 0, 0))
 source.replicate_now()
 await pump(0.15)
 check(client.command("entities").is_empty(), "Hidden entity does not flicker back in hysteresis band")
 source.set_observer_position(peer, server_actor.position)
 source.replicate_now()
 await pump(0.15)
 check(client.command("entities").size() == 1, "Entering base radius restores current membership")
 source.config.capture_mode = 1
 var captures: int = source.get_statistics().captures
 server_actor.position = Vector3(3, 4, 5)
 source.replicate_now()
 await pump(0.15)
 check(source.get_statistics().captures == captures and source.get_statistics().push_skips > 0, "Pushed mode avoids property capture until notified")
 check(client_actor.position == before, "Unmarked pushed property remains unchanged remotely")
 source.mark_dirty()
 check(source.replicate_now() == OK and source.get_statistics().captures == captures + 1, "Push dirtiness captures one changed state")
 await pump(0.2)
 check(client_actor.position == Vector3(3, 4, 5), "Pushed state reaches native client")
 source.config.capture_mode = 0
 source.config.priority = 1
 source.set_session(null)
 await pump(0.3)
 check(server.command("entities").is_empty() and client.command("entities").is_empty(), "Rebinding retires previous owned native entity")
 source.set_session(server)
 check(source.replicate_now() == OK, "Rebound session respawns fresh entity")
 await pump(0.3)
 check(client.command("entities").size() == 1 and late.command("entities").size() == 1, "Rebind baseline reaches both native clients")
 source.config.interest_radius = 8.0
 source.set_observer_position(peer, Vector3(100, 0, 0))
 source.replicate_now()
 await pump(0.3)
 check(client.command("entities").is_empty(), "Observer hidden before disabling interest")
 source.config.interest_radius = 0.0
 source.replicate_now()
 await pump(0.3)
 check(client.command("entities").size() == 1, "Disabling interest restores native visibility")
 client.stop()
 late.stop()
 server.stop()
 check(server.listen(0, "127.0.0.1") == OK and source.replicate_now() == OK, "Restarted server discards stale entity handle and respawns")
 check(server.command("entities").size() == 1, "Restarted server owns exactly one fresh entity")
 source.replication_key = "changed-identity"
 check(server.command("entities").is_empty(), "Changing live identity retires old server entity")
 check(source.replicate_now() == OK and server.command("entities").size() == 1, "Changed identity starts a fresh baseline")
 print("EGP_SUPERPOSITION " + JSON.stringify({"passed": not failed, "checks": checks, "server": source.get_statistics(), "client": replica.get_statistics(), "transport": "two native clients and server in one process"}))
 client.stop()
 late.stop()
 server.stop()
 get_tree().quit(1 if failed else 0)
