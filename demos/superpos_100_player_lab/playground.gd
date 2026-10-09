extends RefCounted

# Shared playground layout: the server creates these native Box3D bodies, the client builds
# matching visuals. Dynamic bodies replicate like any prop; static geometry is never sent.
const BASE := 2200
const STATIC_BASE := 2300
const BOX := 0
const SPHERE := 1
const CAPSULE := 2


static var cached: Array = []


static func dynamic_bodies() -> Array:
	if cached.is_empty():
		cached=_build_dynamic()
	return cached


static func _build_dynamic() -> Array:
	var bodies: Array = []
	# Crate pyramid (4-3-2-1) north of the arena centre.
	var crate := Color("c98a4b")
	for row in range(4):
		for i in range(4-row):
			var x := (i-(3-row)*0.5)*0.92
			bodies.append([BOX,Vector3(x,0.46+row*0.92,18),Vector3.ONE*0.45,0.5,crate])
	# A row of footballs to kick around.
	for i in range(8):
		bodies.append([SPHERE,Vector3(-8.4+i*2.4,0.45,-20.5),Vector3.ONE*0.45,0.25,Color("f2f2f2") if i%2==0 else Color("ff7a59")])
	# Upright barrels on both flanks.
	for i in range(6):
		var side := -1.0 if i<3 else 1.0
		bodies.append([CAPSULE,Vector3(side*20,0.81,-3+(i%3)*3),Vector3(0.38,0.42,0.38),0.6,Color("4f8fd8")])
	# A big light boulder worth a shockwave.
	bodies.append([SPHERE,Vector3(20,1.25,8),Vector3.ONE*1.2,0.08,Color("8c8f99")])
	# Two stacked towers of thin planks.
	for tower in range(2):
		for level in range(6):
			bodies.append([BOX,Vector3(-4+tower*8,0.16+level*0.32,24),Vector3(1.0,0.15,0.3),0.4,Color("e0c27a")])
	return bodies


static func static_bodies() -> Array:
	# Axis-aligned stairs up to a lookout platform, plus cover walls. [centre, half extents, colour]
	var stone := Color("53677d")
	var bodies: Array = []
	for step in range(4):
		var height := 0.3*(step+1)
		bodies.append([Vector3(-27+step*1.0,height*0.5,-6),Vector3(0.5,height*0.5,1.5),stone])
	bodies.append([Vector3(-27,1.2,-1.5),Vector3(2.5,0.15,3),Color("6c86a1")])
	bodies.append([Vector3(-27,0.55,-1.5),Vector3(0.25,0.55,0.25),stone])
	for wall in range(3):
		bodies.append([Vector3(-6+wall*6,0.7,10),Vector3(1.4,0.7,0.2),Color("7a8ea6")])
	return bodies


static func is_dynamic(id: int) -> bool:
	return id>=BASE and id<BASE+dynamic_bodies().size()


static func create(world: EGPBox3DWorld) -> bool:
	var ok := true
	var dynamic := dynamic_bodies()
	for i in range(dynamic.size()):
		var body: Array=dynamic[i]
		var size: Vector3=body[2]
		match int(body[0]):
			BOX:
				ok=ok and world.queue_create_box(BASE+i,0,body[1],size,2,body[3])==OK
			SPHERE:
				ok=ok and world.queue_create_sphere(BASE+i,0,body[1],size.x,2,body[3])==OK
			CAPSULE:
				ok=ok and world.queue_create_capsule(BASE+i,0,body[1],size.x,size.y,2,body[3])==OK
	var fixed := static_bodies()
	for i in range(fixed.size()):
		ok=ok and world.queue_create_box(STATIC_BASE+i,0,fixed[i][0],fixed[i][1],0,1.0)==OK
	return ok


static var cached_static: Array = []


static func support_height(p: Vector3) -> float:
	# Top of the static geometry under p, or the floor. Used for grounded checks.
	if cached_static.is_empty():
		cached_static=static_bodies()
	var top := 0.0
	for body in cached_static:
		var centre: Vector3=body[0]
		var half: Vector3=body[1]
		if absf(p.x-centre.x)<half.x+0.3 and absf(p.z-centre.z)<half.z+0.3:
			top=maxf(top,centre.y+half.y)
	return top


static func mesh_for(body: Array) -> Mesh:
	var size: Vector3=body[2]
	match int(body[0]):
		SPHERE:
			var sphere := SphereMesh.new()
			sphere.radius=size.x
			sphere.height=size.x*2
			return sphere
		CAPSULE:
			var capsule := CapsuleMesh.new()
			capsule.radius=size.x
			capsule.height=(size.y+size.x)*2
			return capsule
	var box := BoxMesh.new()
	box.size=size*2
	return box
