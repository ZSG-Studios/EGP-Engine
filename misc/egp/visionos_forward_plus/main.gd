extends Node3D

var interface: XRInterface
var mock: XRInterfaceExtension
var frames := 0
var elapsed := 0.0
var finished := false

func _ready() -> void:
	var viewport := get_viewport()
	viewport.use_hdr_2d = true
	viewport.msaa_3d = Viewport.MSAA_DISABLED
	viewport.use_taa = false
	viewport.vrs_mode = Viewport.VRS_DISABLED
	viewport.scaling_3d_scale = 1.0
	if "--mock-xr" in OS.get_cmdline_user_args():
		var mock_script = load("res://mock_xr.gd")
		if mock_script == null or not mock_script.can_instantiate():
			fail("Could not load mock XR interface")
			return
		mock = mock_script.new()
		interface = mock
		XRServer.add_interface(interface)
	else:
		interface = XRServer.find_interface("visionOS")
	if interface == null or not interface.initialize():
		fail("XR interface unavailable or initialization failed")
		return
	XRServer.primary_interface = interface
	viewport.use_xr = true
	var origin := XROrigin3D.new()
	add_child(origin)
	origin.current = true
	var camera := XRCamera3D.new()
	camera.near = 0.1
	camera.far = 20.0
	origin.add_child(camera)
	camera.current = true
	var box := MeshInstance3D.new()
	box.mesh = BoxMesh.new()
	box.position = Vector3(0, 0, -2)
	box.scale = Vector3(0.4, 0.4, 0.4)
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = Color(0.8, 0.2, 0.1)
	box.material_override = material
	add_child(box)
	var world := WorldEnvironment.new()
	world.environment = Environment.new()
	world.environment.background_mode = Environment.BG_COLOR
	world.environment.background_color = Color(0, 0, 0, 0)
	world.environment.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	add_child(world)
	print("EGP_VISIONOS_PROBE_STARTED ", JSON.stringify({"renderer": RenderingServer.get_current_rendering_method(), "driver": RenderingServer.get_current_rendering_driver_name(), "mock": mock != null, "device_validated": false}))

func _process(delta: float) -> void:
	if finished:
		return
	elapsed += delta
	frames += 1
	if mock != null:
		if mock.failure != "":
			fail(mock.failure)
		elif mock.snapshots.size() == 2:
			for snapshot in mock.snapshots:
				for eye in snapshot.eyes:
					if eye.lit_pixels < 10 or eye.nearest_depth <= 0.0 or eye.nearest_depth >= 1.0:
						fail("Colour or reverse-Z depth readback failed")
						return
				if absf(snapshot.eyes[0].centroid_x - snapshot.eyes[1].centroid_x) < 0.5:
					fail("Stereo eye images lack expected parallax")
					return
			var result := {"status": "PASS", "scope": "external stereo colour/depth textures and resize; not Apple compositor or hardware", "device_validated": false, "snapshots": mock.snapshots}
			FileAccess.open("user://probe-result.json", FileAccess.WRITE).store_string(JSON.stringify(result, "  "))
			print("EGP_EXTERNAL_STEREO_PASS ", JSON.stringify(result))
			finish(0)
		elif elapsed > 30.0:
			fail("Timed out waiting for rendered stereo readback")
	elif frames % 300 == 0:
		print("EGP_VISIONOS_DEVICE_OBSERVATION frames=", frames, " elapsed=", elapsed, " device_validated=false")

func fail(message: String) -> void:
	push_error(message)
	print("EGP_EXTERNAL_STEREO_FAIL ", message)
	finish(1)

func finish(code: int) -> void:
	finished = true
	RenderingServer.viewport_set_active(get_viewport().get_viewport_rid(), false)
	XRServer.primary_interface = null
	if interface != null:
		interface.uninitialize()
		if mock != null:
			XRServer.remove_interface(interface)
	await RenderingServer.frame_post_draw
	if mock != null:
		RenderingServer.call_on_render_thread(mock.cleanup)
	await RenderingServer.frame_post_draw
	get_tree().quit(code)
