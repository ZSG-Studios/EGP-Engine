class_name SuperposExportLoop
extends SceneTree
# Main loop for exported Superpos probes (run/main_loop_type="SuperposExportLoop").
# An exported executable cannot take --script, so this loop selects the fixture
# from the user arguments and swaps its own script. The running function belongs
# to the replaced script instance, so the fixture's _initialize is deferred: the
# engine can then run the fixture's first _process before it. udp.gd sets itself
# up from whichever comes first and ignores frames until setup succeeded.

func _initialize() -> void:
	var arguments := OS.get_cmdline_user_args()
	var udp := not arguments.is_empty() and arguments[0] in ["server", "client"]
	set_script(load("res://udp.gd" if udp else "res://runtime.gd"))
	call_deferred("_initialize")
