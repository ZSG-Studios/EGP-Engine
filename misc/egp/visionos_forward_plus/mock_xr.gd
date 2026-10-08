extends XRInterfaceExtension
## Exercises the same external colour/depth texture contract without Apple hardware.
var active := false
var size := Vector2i(128, 128)
var color := RID()
var depth := RID()
var rd: RenderingDevice
var tracker: XRPositionalTracker
var rendered := 0
var snapshots: Array[Dictionary] = []
var failure := ""
var retired: Array[RID] = []
var color_slots: Array[RID] = []
var depth_slots: Array[RID] = []

func _get_name() -> StringName:
	return &"EGPExternalStereoProbe"

func _get_capabilities() -> int:
	return XRInterface.XR_STEREO | XRInterface.XR_VR

func _initialize() -> bool:
	tracker = XRPositionalTracker.new()
	tracker.type = XRServer.TRACKER_HEAD
	tracker.name = &"head"
	tracker.set_pose(&"default", Transform3D.IDENTITY, Vector3.ZERO, Vector3.ZERO, XRPose.XR_TRACKING_CONFIDENCE_HIGH)
	XRServer.add_tracker(tracker)
	active = true
	return true

func _is_initialized() -> bool:
	return active

func _uninitialize() -> void:
	active = false
	XRServer.remove_tracker(tracker)

func _get_render_target_size() -> Vector2:
	return Vector2(size)

func _get_view_count() -> int:
	return 2

func _get_camera_transform() -> Transform3D:
	return Transform3D.IDENTITY

func _get_camera_offsets(_tracker_name: StringName) -> Array[Transform3D]:
	return [Transform3D(Basis.IDENTITY, Vector3(-0.032, 0, 0)), Transform3D(Basis.IDENTITY, Vector3(0.032, 0, 0))]

func _get_camera_projections(_tracker_name: StringName, aspect: float, z_near: float, z_far: float) -> Array[Projection]:
	var projection := Projection.create_perspective(70.0, aspect, z_near, z_far, false)
	return [projection, projection]

func _pre_draw_viewport(_target: RID) -> bool:
	if color_slots.is_empty():
		rd = RenderingServer.get_rendering_device()
		var fmt := RDTextureFormat.new()
		fmt.texture_type = RenderingDevice.TEXTURE_TYPE_2D_ARRAY
		fmt.width = size.x
		fmt.height = size.y
		fmt.array_layers = 2
		fmt.format = RenderingDevice.DATA_FORMAT_R16G16B16A16_SFLOAT
		fmt.usage_bits = RenderingDevice.TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT
		for slot in range(3):
			color_slots.append(rd.texture_create(fmt, RDTextureView.new()))
		fmt.format = RenderingDevice.DATA_FORMAT_D32_SFLOAT_S8_UINT
		fmt.usage_bits = RenderingDevice.TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT | RenderingDevice.TEXTURE_USAGE_DEPTH_RESOLVE_ATTACHMENT_BIT
		for slot in range(3):
			depth_slots.append(rd.texture_create(fmt, RDTextureView.new()))
	color = color_slots[rendered % 3]
	depth = depth_slots[rendered % 3]
	return active and color.is_valid() and depth.is_valid()

func _get_color_texture() -> RID:
	return color

func _get_depth_texture() -> RID:
	return depth

func _post_draw_viewport(_target: RID, _rect: Rect2) -> void:
	rendered += 1
	if rendered == 12 or rendered == 28:
		capture()
	if rendered == 16:
		retired.append_array(color_slots)
		retired.append_array(depth_slots)
		color_slots.clear()
		depth_slots.clear()
		color = RID()
		depth = RID()
		size = Vector2i(160, 96)

func capture() -> void:
	var eyes: Array[Dictionary] = []
	for eye in range(2):
		var rgba := rd.texture_get_data(color, eye)
		var z := read_depth(eye)
		if rgba.size() != size.x * size.y * 8 or z.size() < size.x * size.y * 4:
			failure = "Unexpected colour/depth readback size"
			return
		var image := Image.create_from_data(size.x, size.y, false, Image.FORMAT_RGBAH, rgba)
		var count := 0
		var x_sum := 0.0
		var nearest := 0.0
		var stride := 4
		for y in range(size.y):
			for x in range(size.x):
				var pixel := image.get_pixel(x, y)
				if pixel.r > 0.2 and pixel.a > 0.5:
					count += 1
					x_sum += x
				nearest = maxf(nearest, z.decode_float((y * size.x + x) * stride))
		image.convert(Image.FORMAT_RGBA8)
		image.save_png(evidence_path("probe_%d_eye_%d.png" % [rendered, eye]))
		eyes.append({"lit_pixels": count, "centroid_x": x_sum / maxf(count, 1), "nearest_depth": nearest})
	snapshots.append({"size": [size.x, size.y], "eyes": eyes})
	print("EGP_STEREO_SNAPSHOT ", JSON.stringify(snapshots[-1]))

# Copy sampled depth into a float buffer: raw depth/stencil readback layouts differ
# across graphics APIs and must not be interpreted as interleaved float pixels.
func read_depth(eye: int) -> PackedByteArray:
	var source := RDShaderSource.new()
	source.source_compute = """#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2DArray depth_texture;
layout(set = 0, binding = 1, std430) restrict buffer Result { float values[]; } result;
layout(push_constant, std430) uniform Params { int width; int height; int eye; int pad; } params;
void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (p.x >= params.width || p.y >= params.height) return;
    result.values[p.y * params.width + p.x] = texelFetch(depth_texture, ivec3(p, params.eye), 0).r;
}
"""
	var spirv := rd.shader_compile_spirv_from_source(source)
	if spirv.compile_error_compute != "":
		failure = spirv.compile_error_compute
		return PackedByteArray()
	var shader := rd.shader_create_from_spirv(spirv)
	var pipeline := rd.compute_pipeline_create(shader)
	var sampler := rd.sampler_create(RDSamplerState.new())
	var buffer := rd.storage_buffer_create(size.x * size.y * 4)
	var tex_uniform := RDUniform.new()
	tex_uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_SAMPLER_WITH_TEXTURE
	tex_uniform.binding = 0
	tex_uniform.add_id(sampler)
	tex_uniform.add_id(depth)
	var output_uniform := RDUniform.new()
	output_uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	output_uniform.binding = 1
	output_uniform.add_id(buffer)
	var uniforms := rd.uniform_set_create([tex_uniform, output_uniform], shader, 0)
	var commands := rd.compute_list_begin()
	rd.compute_list_bind_compute_pipeline(commands, pipeline)
	rd.compute_list_bind_uniform_set(commands, uniforms, 0)
	rd.compute_list_set_push_constant(commands, PackedInt32Array([size.x, size.y, eye, 0]).to_byte_array(), 16)
	rd.compute_list_dispatch(commands, ceili(size.x / 8.0), ceili(size.y / 8.0), 1)
	rd.compute_list_end()
	var data := rd.buffer_get_data(buffer)
	for resource in [uniforms, pipeline, shader, sampler, buffer]:
		rd.free_rid(resource)
	return data

func cleanup() -> void:
	for resource in retired + color_slots + depth_slots:
		if resource.is_valid():
			rd.free_rid(resource)
	retired.clear()
	color_slots.clear()
	depth_slots.clear()
	color = RID()
	depth = RID()

func evidence_path(filename: String) -> String:
	var directory := OS.get_environment("EGP_STEREO_OUTPUT")
	if directory.is_empty():
		directory = "user://"
	return directory.path_join(filename)
