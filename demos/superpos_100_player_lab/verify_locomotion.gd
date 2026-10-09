extends SceneTree
const VIEW := preload("res://character_view.gd")
var failures: Array[String] = []
func _initialize() -> void:
	_run.call_deferred()
func _run() -> void:
	var rows := []
	for female in [false,true]:
		var view := VIEW.new()
		root.add_child(view)
		if not view.configure(Color.WHITE,female):
			failures.append("load")
			continue
		var identities := {}
		for mode in ["walk","jog","sprint","crouch","crawl"]:
			var speed: float={"walk":2.2,"jog":4.5,"sprint":8.5,"crouch":2.0,"crawl":1.2}[mode]
			var stance: int={"walk":0,"jog":0,"sprint":4,"crouch":8,"crawl":16}[mode]
			for direction in range(8):
				var angle := direction*TAU/8
				view.present(Vector3(sin(angle),0,cos(angle))*speed,1|stance,0)
				for i in range(20):
					view._process(0.05)
				view.skeleton.force_update_all_bone_transforms()
				var fingerprint := ""
				var changed := 0
				for bone in range(view.skeleton.get_bone_count()):
					var q: Quaternion=view.skeleton.get_bone_pose_rotation(bone)
					fingerprint+="%.3f,%.3f,%.3f,%.3f;"%[q.x,q.y,q.z,q.w]
					if absf(q.w)<0.9999:
						changed+=1
				if changed<3 or view.current!=mode:
					failures.append(mode+str(direction)+" female="+str(female)+" state="+view.current)
				identities[fingerprint]=true
				rows.append({"female":female,"mode":mode,"direction":direction,"state":view.current,"posed_bones":changed})
		if identities.size()<32:
			failures.append("directional poses collapsed")
		for test in [[32,"Push_Loop"],[64,"GroundSit_Idle_Loop"],[128,"Sitting_Idle_Loop"],[512,"Swim_Idle_Loop"],[256,"Death02" if female else "Death01"]]:
			view.present(Vector3.ZERO,1|int(test[0]),0)
			for i in range(80):
				view._process(0.05)
			if view.current!=test[1]:
				failures.append("activity expected="+test[1]+" actual="+view.current)
			rows.append({"female":female,"activity":test[1],"state":view.current})
		view.queue_free()
	var file := FileAccess.open(ProjectSettings.globalize_path("res://../../.build/diagnostics/superpos-100/locomotion-receipt.json"),FileAccess.WRITE)
	file.store_string(JSON.stringify({"passed":failures.is_empty(),"failures":failures,"checks":rows},"\t"))
	print("LOCOMOTION ","PASS" if failures.is_empty() else "FAIL"," checks=",rows.size()," failures=",failures)
	quit(0 if failures.is_empty() else 1)
