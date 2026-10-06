extends Node
const FIXTURES := {
	"aio": "res://Smoke.tscn",
	"physics": "res://Physics.tscn",
	"token": "res://Token.tscn",
	"prediction": "res://Prediction.tscn",
	"lifecycle": "res://Lifecycle.tscn",
	"processes": "res://Processes.tscn",
}

func _ready() -> void:
	_open_fixture.call_deferred()

func _open_fixture() -> void:
	var fixture := "aio"
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--fixture="):
			fixture = argument.trim_prefix("--fixture=")
	if not FIXTURES.has(fixture):
		push_error("Unknown networking fixture")
		get_tree().quit(2)
		return
	var tree := get_tree()
	if tree.change_scene_to_file(FIXTURES[fixture]) != OK:
		push_error("Could not load networking fixture")
		tree.quit(2)
