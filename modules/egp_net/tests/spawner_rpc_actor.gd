extends Node

var owner_value: int = 0
var authority_text: String = ""
var any_count: int = 0

func owner_action(value: int) -> void:
	owner_value = value

func authority_action(value: String) -> void:
	authority_text = value

func any_action(value: bool) -> void:
	if value:
		any_count += 1
