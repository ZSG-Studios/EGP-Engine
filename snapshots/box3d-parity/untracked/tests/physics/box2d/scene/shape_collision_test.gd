extends SceneTree

func _initialize() -> void:
	var circle := CircleShape2D.new()
	circle.radius = 10
	var other := CircleShape2D.new()
	other.radius = 10
	var origin := Transform2D.IDENTITY
	var overlap := Transform2D(0, Vector2(15, 0))
	var apart := Transform2D(0, Vector2(100, 0))
	var passed := circle.collide(origin, other, overlap) and not circle.collide(origin, other, apart)
	var contacts := circle.collide_and_get_contacts(origin, other, overlap)
	passed = passed and contacts.size() == 2 and absf(contacts[0].distance_to(contacts[1]) - 5) < 0.01
	passed = passed and circle.collide_with_motion(origin, Vector2(100, 0), other, apart, Vector2.ZERO)
	passed = passed and not circle.collide_with_motion(origin, Vector2(-100, 0), other, apart, Vector2.ZERO)
	# Concave shape data consists of pairs, not a connected point strip.
	var segments := ConcavePolygonShape2D.new()
	segments.segments = PackedVector2Array([Vector2(-20, -30), Vector2(20, -30), Vector2(-20, 30), Vector2(20, 30)])
	passed = passed and not circle.collide(origin, segments, origin)
	print("RESULT: PASS - primitive contacts, swept collision and disjoint concave segments" if passed else "RESULT: FAIL - shape collision API")
	quit(0 if passed else 1)
